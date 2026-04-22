// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;

using EpicGames.Core;
using EpicGames.Perforce;
using Microsoft.Extensions.Logging;
using UnrealGameSync;
using UnrealGameSyncCmd.Utils;

using UserErrorException = UnrealGameSyncCmd.Exceptions.UserErrorException;

namespace UnrealGameSyncCmd.Commands
{
	internal class SyncCommandOptions
	{
		[CommandLine("-Clean")]
		public bool Clean { get; set; }

		[CommandLine("-Build")]
		public bool Build { get; set; }

		[CommandLine("-Binaries")]
		public bool Binaries { get; set; }

		[CommandLine("-NoGPF", Value = "false")]
		[CommandLine("-NoProjectFiles", Value = "false")]
		public bool ProjectFiles { get; set; } = true;

		[CommandLine("-Clobber")]
		public bool Clobber { get; set; }

		[CommandLine("-Refilter")]
		public bool Refilter { get; set; }

		[CommandLine("-Only")]
		public bool SingleChange { get; set; }
	}

	internal class SyncCommand : Command
	{
		static async Task<bool> IsCodeChangeAsync(IPerforceConnection perforce, int change)
		{
			DescribeRecord describeRecord = await perforce.DescribeAsync(change);
			return IsCodeChange(describeRecord);
		}

		static bool IsCodeChange(DescribeRecord describeRecord)
		{
			foreach (DescribeFileRecord file in describeRecord.Files)
			{
				if (PerforceUtils.CodeExtensions.Any(extension => file.DepotFile.EndsWith(extension, StringComparison.OrdinalIgnoreCase)))
				{
					return true;
				}
			}
			return false;
		}

		public override async Task ExecuteAsync(CommandContext context)
		{
			ILogger logger = context.Logger;
			context.Arguments.TryGetPositionalArgument(out string? changeString);

			SyncCommandOptions syncOptions = new SyncCommandOptions();
			context.Arguments.ApplyTo(syncOptions);

			context.Arguments.CheckAllArgumentsUsed();

			UserWorkspaceSettings settings = UserSettingsUtils.ReadRequiredUserWorkspaceSettings();
			using IPerforceConnection perforceClient = await PerforceConnectionUtils.ConnectAsync(settings, context.LoggerFactory);
			WorkspaceStateWrapper state = await WorkspaceStateUtils.ReadWorkspaceState(perforceClient, settings, context.UserSettings, logger);

			changeString ??= "latest";

			ProjectInfo projectInfo = new ProjectInfo(settings.RootDir, state.Current);
			UserProjectSettings projectSettings = context.UserSettings.FindOrAddProjectSettings(projectInfo, settings, logger);

			ConfigFile projectConfig = await ConfigUtils.ReadProjectConfigFileAsync(perforceClient, projectInfo, logger, CancellationToken.None);

			bool syncLatest = String.Equals(changeString, "latest", StringComparison.OrdinalIgnoreCase);

			int change;
			if (!Int32.TryParse(changeString, out change))
			{
				if (syncLatest)
				{
					List<ChangesRecord> changes = await perforceClient.GetChangesAsync(ChangesOptions.None, 1, ChangeStatus.Submitted, $"//{settings.ClientName}/...");
					change = changes[0].Number;
				}
				else
				{
					throw new UserErrorException("Unknown change type for sync '{Change}'", changeString);
				}
			}

			WorkspaceUpdateOptions options = syncOptions.SingleChange ? WorkspaceUpdateOptions.SyncSingleChange : WorkspaceUpdateOptions.Sync;
			if (syncOptions.Clean)
			{
				options |= WorkspaceUpdateOptions.Clean;
			}
			if (syncOptions.Build)
			{
				options |= WorkspaceUpdateOptions.Build;
			}
			if (syncOptions.ProjectFiles)
			{
				options |= WorkspaceUpdateOptions.GenerateProjectFiles;
			}
			if (syncOptions.Clobber)
			{
				options |= WorkspaceUpdateOptions.Clobber;
			}
			if (syncOptions.Refilter)
			{
				options |= WorkspaceUpdateOptions.Refilter;
			}
			options |= WorkspaceUpdateContext.GetOptionsFromConfig(context.UserSettings.Global, settings);
			options |= WorkspaceUpdateOptions.RemoveFilteredFiles;

			string[] syncFilter = SyncFilterUtils.ReadSyncFilter(settings, context.UserSettings, projectConfig, state.Current.ProjectIdentifier);

			using WorkspaceLock? workspaceLock = CreateWorkspaceLock(settings.RootDir);
			if (workspaceLock != null && !await workspaceLock.TryAcquireAsync())
			{
				logger.LogError("Another process is already syncing this workspace.");
				return;
			}

			WorkspaceUpdateContext updateContext = new WorkspaceUpdateContext(change, options, BuildConfig.Development, syncFilter, projectSettings.BuildSteps, null);
			updateContext.PerforceSyncOptions = context.UserSettings.Global.Perforce;

			if (syncOptions.Binaries)
			{
				List<BaseArchiveChannel> archives = await BaseArchive.EnumerateChannelsAsync(perforceClient, context.HordeClient, context.CloudStorage, projectConfig, state.Current.ProjectIdentifier, CancellationToken.None);

				BaseArchiveChannel? editorArchiveInfo = archives.FirstOrDefault(x => x.Type == IArchiveChannel.EditorArchiveType);
				if (editorArchiveInfo == null)
				{
					throw new UserErrorException("No editor archives found for project");
				}

				KeyValuePair<int, IArchive> revision = editorArchiveInfo.ChangeNumberToArchive.LastOrDefault(x => x.Key <= change);
				if (revision.Key == 0)
				{
					throw new UserErrorException($"No editor archives found for CL {change}");
				}

				if (revision.Key < change)
				{
					int lastChange = revision.Key;

					List<ChangesRecord> changeRecords = await perforceClient.GetChangesAsync(ChangesOptions.None, 1, ChangeStatus.Submitted, $"//{settings.ClientName}/...@{revision.Key + 1},{change}");
					foreach (ChangesRecord changeRecord in changeRecords.OrderBy(x => x.Number))
					{
						if (await IsCodeChangeAsync(perforceClient, changeRecord.Number))
						{
							if (syncLatest)
							{
								updateContext.ChangeNumber = lastChange;
							}
							else
							{
								throw new UserErrorException($"No editor binaries found for CL {change} (last archive at CL {revision.Key}, but CL {changeRecord.Number} is a code change)");
							}
							break;
						}
						change = changeRecord.Number;
					}
				}

				updateContext.Options |= WorkspaceUpdateOptions.SyncArchives;
				updateContext.ArchiveTypeToArchive[IArchiveChannel.EditorArchiveType] = revision.Value;
			}

			WorkspaceUpdate update = new WorkspaceUpdate(updateContext);
			(WorkspaceUpdateResult result, string message) = await update.ExecuteAsync(perforceClient.Settings, projectInfo, state, context.Logger, CancellationToken.None);
			if (result == WorkspaceUpdateResult.FilesToClobber)
			{
				logger.LogWarning("The following files are modified in your workspace:");
				foreach (string file in updateContext.ClobberFiles.Keys.OrderBy(x => x))
				{
					logger.LogWarning("  {File}", file);
				}
				logger.LogWarning("Use -Clobber to overwrite");
			}

			state.Modify(x => x.SetLastSyncState(result, updateContext, message));

			if (result != WorkspaceUpdateResult.Success)
			{
				throw new UserErrorException("{Message} (Result: {Result})", message, result);
			}
		}

		static WorkspaceLock? CreateWorkspaceLock(DirectoryReference rootDir)
		{
			if (RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
			{
				return new WorkspaceLock(rootDir);
			}
			else
			{
				return null;
			}
		}
	}
}

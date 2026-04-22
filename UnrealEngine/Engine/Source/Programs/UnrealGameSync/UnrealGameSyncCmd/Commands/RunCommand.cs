// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
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
	internal class RunCommand : Command
	{
		public override async Task ExecuteAsync(CommandContext context)
		{
			ILogger logger = context.Logger;

			UserWorkspaceSettings settings = UserSettingsUtils.ReadRequiredUserWorkspaceSettings();
			using IPerforceConnection perforceClient = await PerforceConnectionUtils.ConnectAsync(settings, context.LoggerFactory);
			WorkspaceStateWrapper state = await WorkspaceStateUtils.ReadWorkspaceState(perforceClient, settings, context.UserSettings, logger);

			ProjectInfo projectInfo = new ProjectInfo(settings.RootDir, state.Current);
			ConfigFile projectConfig = await ConfigUtils.ReadProjectConfigFileAsync(perforceClient, projectInfo, logger, CancellationToken.None);

			FileReference receiptFile = ConfigUtils.GetEditorReceiptFile(projectInfo, projectConfig, EditorConfig);
			logger.LogDebug("Receipt file: {Receipt}", receiptFile);

			if (!ConfigUtils.TryReadEditorReceipt(projectInfo, receiptFile, out TargetReceipt? receipt) || String.IsNullOrEmpty(receipt.Launch))
			{
				throw new UserErrorException("The editor needs to be built before you can run it. (Missing {ReceiptFile}).", receiptFile);
			}
			if (!File.Exists(receipt.Launch))
			{
				throw new UserErrorException("The editor needs to be built before you can run it. (Missing {LaunchFile}).", receipt.Launch);
			}

			List<string> launchArguments = new List<string>();
			if (settings.LocalProjectPath.HasExtension(ProjectUtils.UProjectExtension))
			{
				launchArguments.Add($"\"{settings.LocalProjectPath}\"");
			}
			if (EditorConfig == BuildConfig.Debug || EditorConfig == BuildConfig.DebugGame)
			{
				launchArguments.Append(" -debug");
			}
			for (int idx = 0; idx < context.Arguments.Count; idx++)
			{
				if (!context.Arguments.HasBeenUsed(idx))
				{
					launchArguments.Add(context.Arguments[idx]);
				}
			}

			string commandLine = CommandLineArguments.Join(launchArguments);
			logger.LogInformation("Spawning: {LaunchFile} {CommandLine}", CommandLineArguments.Quote(receipt.Launch), commandLine);

			if (!Utility.SpawnProcess(receipt.Launch, commandLine))
			{
				logger.LogError("Unable to spawn {App} {Args}", receipt.Launch, launchArguments.ToString());
			}
		}
	}
}

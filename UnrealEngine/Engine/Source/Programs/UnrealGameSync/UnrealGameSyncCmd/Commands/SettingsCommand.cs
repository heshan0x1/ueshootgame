// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.IO;
using System.Text.Json;
using System.Threading.Tasks;
using EpicGames.Core;
using Microsoft.Extensions.Logging;
using UnrealGameSync;
using UnrealGameSyncCmd.Utils;

namespace UnrealGameSyncCmd.Commands
{
	internal class SettingsCommandOptions
	{
		[CommandLine("-Global=")]
		public string Global { get; set; } = String.Empty;

		[CommandLine("-User=")]
		public string User { get; set; } = String.Empty;
	}

	internal class SettingsCommand : Command
	{
		public override async Task ExecuteAsync(CommandContext context)
		{
			ILogger logger = context.Logger;

			SettingsCommandOptions options = context.Arguments.ApplyTo<SettingsCommandOptions>(logger);
			context.Arguments.CheckAllArgumentsUsed();

			if (!String.IsNullOrWhiteSpace(options.Global))
			{
				if (context.GlobalSettings != null)
				{
					logger.LogInformation("Writing global settings to {Global}", options.Global);
					await WriteSettings(options.Global, context.GlobalSettings);
				}
				else if (context.UserSettings != null)
				{
					logger.LogInformation("Writing global settings to {Global}", options.Global);
					await WriteSettings(options.Global, context.UserSettings);
				}
			}

			if (!String.IsNullOrWhiteSpace(options.User))
			{
				UserWorkspaceSettings userWorkspaceSettings = UserSettingsUtils.ReadRequiredUserWorkspaceSettings();
				logger.LogInformation("Writing user settings to {User}", options.User);
				await WriteSettings(options.User, userWorkspaceSettings);
			}
		}

		private static async Task WriteSettings(string path, object? settings)
		{
			if (String.IsNullOrWhiteSpace(path) || settings == null)
			{
				return;
			}

			string trimmed = path.Trim('\"')
				.Replace(Path.AltDirectorySeparatorChar, Path.DirectorySeparatorChar);
			if (!Path.IsPathFullyQualified(trimmed))
			{
				return;
			}

			string? directory = Path.GetDirectoryName(trimmed);
			if (!String.IsNullOrWhiteSpace(directory))
			{
				Directory.CreateDirectory(directory);
			}

			JsonSerializerOptions serializerOptions = new JsonSerializerOptions()
			{
				WriteIndented = true
			};
			string json = JsonSerializer.Serialize(settings, serializerOptions);

			await File.WriteAllTextAsync(trimmed, json);
		}
	}
}

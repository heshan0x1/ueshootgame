// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Horde;
using Microsoft.Extensions.Logging;
using UnrealGameSync;

namespace UnrealGameSyncCmd
{
	internal class CommandContext
	{
		public CommandLineArguments Arguments { get; }
		public ILogger Logger { get; }
		public ILoggerFactory LoggerFactory { get; }

		public UserSettings? GlobalSettings { get; }

		public GlobalSettingsFile UserSettings { get; }

		public IHordeClient? HordeClient { get; }
		public ICloudStorage? CloudStorage { get; }

		public CommandContext(CommandLineArguments arguments, ILogger logger, ILoggerFactory loggerFactory, GlobalSettingsFile userSettings, UserSettings? globalSettings, IHordeClient? hordeClient, ICloudStorage? cloudStorage)
		{
			Arguments = arguments;
			Logger = logger;
			LoggerFactory = loggerFactory;
			GlobalSettings = globalSettings;
			UserSettings = userSettings;
			HordeClient = hordeClient;
			CloudStorage = cloudStorage;
		}
	}
}

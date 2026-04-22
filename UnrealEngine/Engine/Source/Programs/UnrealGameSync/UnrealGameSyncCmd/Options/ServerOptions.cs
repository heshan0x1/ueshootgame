// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;

namespace UnrealGameSyncCmd.Options
{
	internal class ServerOptions
	{
		[CommandLine("-Server=")]
		public string? ServerAndPort { get; set; }

		[CommandLine("-User=")]
		public string? UserName { get; set; }
	}
}

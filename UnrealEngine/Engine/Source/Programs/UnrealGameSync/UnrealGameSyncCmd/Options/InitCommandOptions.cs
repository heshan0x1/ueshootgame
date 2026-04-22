// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;

namespace UnrealGameSyncCmd.Options
{
	internal class InitCommandOptions : ConfigCommandOptions
	{
		[CommandLine("-Client=")]
		public string? ClientName { get; set; }

		[CommandLine("-Branch=")]
		public string? BranchPath { get; set; }

		[CommandLine("-Project=")]
		public string? ProjectName { get; set; }

		[CommandLine("-ClientRoot=")]
		public string? ClientRoot { get; set; }

		[CommandLine("-IgnoreExistingClients")]
		public bool IgnoreExistingClients { get; set; }
	}
}

// Copyright Epic Games, Inc. All Rights Reserved.

using System.Threading.Tasks;
using UnrealGameSyncCmd.Utils;

namespace UnrealGameSyncCmd.Commands
{
	internal class UninstallCommand : Command
	{
		public override async Task ExecuteAsync(CommandContext context)
		{
			await InstallUtils.UpdateInstallAsync(false, context.Logger);
		}
	}
}

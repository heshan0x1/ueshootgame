// Copyright Epic Games, Inc. All Rights Reserved.

using System.Threading.Tasks;
using UnrealGameSyncCmd.Utils;

namespace UnrealGameSyncCmd.Commands
{
	internal class InstallCommand : Command
	{
		public override async Task ExecuteAsync(CommandContext context)
		{
			await InstallUtils.UpdateInstallAsync(true, context.Logger);
		}
	}
}

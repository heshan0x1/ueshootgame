// Copyright Epic Games, Inc. All Rights Reserved.

using System.Threading.Tasks;
using Microsoft.Extensions.Logging;
using UnrealGameSyncCmd.Utils;

namespace UnrealGameSyncCmd.Commands
{
	internal class VersionCommand : Command
	{
		public override Task ExecuteAsync(CommandContext context)
		{
			ILogger logger = context.Logger;

			string version = VersionUtils.GetVersion();
			logger.LogInformation("UnrealGameSync {Version}", version);

			return Task.CompletedTask;
		}
	}
}

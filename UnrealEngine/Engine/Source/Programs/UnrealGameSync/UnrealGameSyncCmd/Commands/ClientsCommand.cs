// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Threading.Tasks;
using Microsoft.Extensions.Logging;

using EpicGames.Perforce;

using UnrealGameSyncCmd.Options;
using UnrealGameSyncCmd.Utils;

namespace UnrealGameSyncCmd.Commands
{
	internal class ClientsCommandOptions : ServerOptions
	{
	}

	internal class ClientsCommand : Command
	{
		public override async Task ExecuteAsync(CommandContext context)
		{
			ILogger logger = context.Logger;

			ClientsCommandOptions options = context.Arguments.ApplyTo<ClientsCommandOptions>(logger);
			context.Arguments.CheckAllArgumentsUsed();

			using IPerforceConnection perforceClient = await PerforceConnectionUtils.ConnectAsync(options.ServerAndPort, options.UserName, null, context.LoggerFactory);
			InfoRecord info = await perforceClient.GetInfoAsync(InfoOptions.ShortOutput);

			List<ClientsRecord> clients = await perforceClient.GetClientsAsync(EpicGames.Perforce.ClientsOptions.None, perforceClient.Settings.UserName);
			foreach (ClientsRecord client in clients)
			{
				if (String.Equals(info.ClientHost, client.Host, StringComparison.OrdinalIgnoreCase))
				{
					logger.LogInformation("{Client,-50} {Root}", client.Name, client.Root);
				}
			}
		}
	}
}

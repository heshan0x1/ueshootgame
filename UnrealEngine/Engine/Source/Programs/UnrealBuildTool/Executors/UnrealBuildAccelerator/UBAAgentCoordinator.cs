// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Core;
using EpicGames.UBA;

namespace UnrealBuildTool
{
	// statusRow, statusColumn, statusText, statusType, statusLink
	using StatusUpdateAction = Action<uint, uint, string, LogEntryType, string?>;

	interface IUbaAgentCoordinatorScheduler
	{
		bool IsEmpty { get; }
		double GetProcessWeightThatCanRunRemotelyNow();
		bool AddClient(string ip, int port, string crypto = "");
	}

	interface IUBAAgentCoordinator
	{
		DirectoryReference? GetUBARootDir();

		Task InitAsync(UBAExecutor executor);

		UnrealBuildAcceleratorCacheConfig? RequestCacheServer(CancellationToken cancellationToken);

		void Start(IUbaAgentCoordinatorScheduler scheduler, Func<LinkedAction, bool> canRunRemotely, StatusUpdateAction updateStatus);

		void Stop();

		Task CloseAsync();

		void Done();
	}
}
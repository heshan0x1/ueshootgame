// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Perforce.Fixture;
using HordeCommon.Rpc.Messages;
using JobDriver.Execution;
using Microsoft.Extensions.Logging;
using Microsoft.VisualStudio.TestTools.UnitTesting;
using OpenTelemetry.Trace;

namespace JobDriver.Tests;

[TestClass]
public class PerforceMaterializerTests : BasePerforceFixtureTest
{
	private static Tracer NoOpTracer { get; } = TracerProvider.Default.GetTracer("NoOp");

	[TestMethod]
	public async Task WalkChangeListsWithReusedMaterializerAsync()
	{
		CancellationToken ct = CancellationToken.None;
		using PerforceMaterializer pm = CreatePm(Fixture.StreamFooMain);
		List<StreamState> states = [Main(2), Main(3), Main(4), Main(5), Main(6), Main(7), Main(7, 8)];
		states.AddRange(states.SkipLast(1).Reverse());
		foreach (StreamState ss in states)
		{
			await pm.SyncAsync(ss.Cl, ss.ShelvedCl, new SyncOptions(), ct);
			Fixture.StreamFooMain.GetChangelist(ss.ShelvedCl > 0 ? ss.ShelvedCl : ss.Cl).AssertDepotFiles(pm.DirectoryPath.FullName);
			await pm.FinalizeAsync(ct);
		}
	}

	[TestMethod]
	public async Task SyncLatestAsync()
	{
		using PerforceMaterializer pm = CreatePm(Fixture.StreamFooMain);
		
		await pm.SyncAsync(IWorkspaceMaterializer.LatestChangeNumber, -1, new SyncOptions(), CancellationToken.None);
		Fixture.StreamFooMain.GetChangelist(7).AssertDepotFiles(pm.DirectoryPath.FullName);
		await pm.FinalizeAsync(CancellationToken.None);

		PerforceMaterializer.State? state = await pm.LoadStateForTestAsync(CancellationToken.None);
		Assert.AreEqual(7, state!.Changelist);
	}

	[TestMethod]
	public async Task TestAllStatesAsync()
	{
		StreamState[] mainState = [Main(2), Main(4), Main(5), Main(7), Main(7, 8)];
		StreamState[] releaseStates = [Release(10), Release(11)];
		
		StreamState?[] localStates = [null, ..mainState, ..releaseStates];
		StreamState?[] serverStates = [..localStates];
		StreamState[] targetStates = [..mainState, ..releaseStates];

		foreach (StreamState? localState in localStates)
		{
			foreach (StreamState? serverState in serverStates)
			{
				foreach (StreamState targetState in targetStates)
				{
					await SyncWithScenarioAsync(localState, serverState, targetState, CancellationToken.None);
				}
			}
		}
	}

	private StreamState Main(int cl, int shelvedCl = 0) => new (Fixture.StreamFooMain, cl, shelvedCl);
	private StreamState Release(int cl, int shelvedCl = 0) => new (Fixture.StreamFooRelease, cl, shelvedCl);
	private record StreamState(StreamFixture Stream, int Cl, int ShelvedCl = 0)
	{
		public override string ToString()
		{
			return $"Stream={Stream.Root} CL={Cl} Shelve={ShelvedCl}";
		}
	}
	private record SyncScenario(StreamState? LocalState, StreamState? ServerState, StreamState TargetState);

	private Task SyncWithScenarioAsync(StreamState? localState, StreamState? serverState, StreamState targetState, CancellationToken cancellationToken)
	{
		return SyncWithScenarioAsync(new SyncScenario(localState, serverState, targetState), cancellationToken);
	}

	private async Task SyncWithScenarioAsync(SyncScenario scenario, CancellationToken cancellationToken)
	{
		RemoveTempDir();
		CreateTempDir();
        
		if (scenario.LocalState != null)
		{
			// Initialize a local state with files corresponding to given stream state, but delete the client
			Console.WriteLine($"Preparing local state {scenario.LocalState} ...");
			using PerforceMaterializer pm = CreatePm(scenario.LocalState.Stream);
			await SyncToStateAsync(pm, scenario.LocalState, cancellationToken);
			await pm.DeleteClientForTestAsync(cancellationToken);
		}
		
		if (scenario.ServerState != null)
		{
			Console.WriteLine($"Preparing server state {scenario.ServerState} ...");
			
			string stateDir = TempDir.FullName;
			string backupDir = TempDir.FullName + "-backup";
			if (Directory.Exists(stateDir))
			{
				// Save the original local state so the sync below cannot overwrite
				Directory.Move(stateDir, backupDir);
			}
			
            using PerforceMaterializer pm = CreatePm(scenario.ServerState.Stream);
			await SyncToStateAsync(pm, scenario.ServerState, cancellationToken);

			// Now that the workspace has been synced, reset the local files back
			DeletePerforceDir(stateDir);
			if (Directory.Exists(backupDir))
			{
				Directory.Move(backupDir, stateDir);
			}
		}

		{
			Console.WriteLine($" Local state: {scenario.LocalState}");
			Console.WriteLine($"Server state: {scenario.ServerState}");
			Console.WriteLine($"Target state: {scenario.TargetState}");
			Console.WriteLine("Syncing ...");
			using PerforceMaterializer pm = CreatePm(scenario.TargetState.Stream);
			await SyncToStateAsync(pm, scenario.TargetState, cancellationToken);
		}
	}

	private static async Task SyncToStateAsync(PerforceMaterializer pm, StreamState state, CancellationToken cancellationToken)
	{
		await pm.SyncAsync(state.Cl, state.ShelvedCl, new SyncOptions(), cancellationToken);
		state.Stream.GetChangelist(state.ShelvedCl > 0 ? state.ShelvedCl : state.Cl).AssertDepotFiles(pm.DirectoryPath.FullName);
		await pm.FinalizeAsync(cancellationToken);
		
		PerforceMaterializer.State? pmState = await pm.LoadStateForTestAsync(cancellationToken);
		Assert.AreEqual(state.Cl, pmState!.Changelist);
		Assert.AreEqual(state.ShelvedCl, pmState!.ShelvedChangelist);
		Assert.AreEqual(state.Stream.Root, pmState.Stream);
		Assert.AreEqual(PerforceMaterializer.TransactionStatus.Clean, pmState.Status);
	}

	private PerforceMaterializer CreatePm(StreamFixture stream, string? dataDir = null, string? identifier = "pmTest")
	{
		ILogger logger = LoggerFactory.CreateLogger("PM");
		RpcAgentWorkspace raw = new ()
		{
			ServerAndPort = PerforceConnection.ServerAndPort,
			UserName = PerforceConnection.UserName,
			Password = PerforceConnection.Settings.Password,
			Identifier = $"{TestGuid}-{identifier}",
			Stream = stream.Root,
		};
		PerforceMaterializerOptions options = new(dataDir ?? TempDir.FullName, raw);
		return new PerforceMaterializer(options, NoOpTracer, logger);
	}
}


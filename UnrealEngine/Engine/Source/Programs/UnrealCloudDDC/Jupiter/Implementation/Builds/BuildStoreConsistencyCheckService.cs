// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Horde.Storage;
using EpicGames.Serialization;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Options;
using OpenTelemetry.Trace;

namespace Jupiter.Implementation
{
	public class BuildStoreConsistencyState
	{
	}

	// ReSharper disable once ClassNeverInstantiated.Global
	public class BuildStoreConsistencyCheckService : PollingService<BuildStoreConsistencyState>
	{
		private readonly IOptionsMonitor<ConsistencyCheckSettings> _settings;
		private readonly ILeaderElection _leaderElection;
		private readonly IBuildStore _buildStore;
		private readonly Tracer _tracer;
		private readonly ILogger _logger;

		protected override bool ShouldStartPolling()
		{
			return _settings.CurrentValue.EnableBuildStoreConsistencyCheck;
		}

		public BuildStoreConsistencyCheckService(IOptionsMonitor<ConsistencyCheckSettings> settings, ILeaderElection leaderElection, IBuildStore buildStore, Tracer tracer, ILogger<BuildStoreConsistencyCheckService> logger) : base(serviceName: nameof(BuildStoreConsistencyCheckService), TimeSpan.FromSeconds(settings.CurrentValue.ConsistencyCheckPollFrequencySeconds), new BuildStoreConsistencyState(), logger)
		{
			_settings = settings;
			_leaderElection = leaderElection;
			_buildStore = buildStore;
			_tracer = tracer;
			_logger = logger;
		}

		public override async Task<bool> OnPollAsync(BuildStoreConsistencyState state, CancellationToken cancellationToken)
		{
			bool shouldEnable = _settings.CurrentValue.EnableBuildStoreConsistencyCheck;
			if (!shouldEnable)
			{
				_logger.LogInformation("Skipped running build store consistency check as it is disabled");
				return false;
			}

			if (!_leaderElection.IsThisInstanceLeader())
			{
				_logger.LogInformation("Skipped running build store consistency check because this instance was not the leader");
				return false;
			}

			await RunConsistencyCheckAsync(cancellationToken);

			return true;
		}
		
		private async Task RunConsistencyCheckAsync(CancellationToken cancellationToken)
		{
			ulong countOfBuildsChecked = 0;
			ulong countOfIncorrectBuildsFound = 0;
			await Parallel.ForEachAsync(_buildStore.ListAllBuildsAsync(cancellationToken), new ParallelOptions
			{
				CancellationToken = cancellationToken,
				MaxDegreeOfParallelism = _settings.CurrentValue.BlobIndexMaxParallelOperations,
			},
				async (tuple, token) =>
				{
					(NamespaceId ns, BucketId bucket, CbObjectId buildId) = tuple;
					Interlocked.Increment(ref countOfBuildsChecked);

					if (countOfBuildsChecked % 100 == 0)
					{
						_logger.LogInformation("Consistency check running on build store, count of builds processed so far: {CountOfBuilds}", countOfBuildsChecked);
					}

					using TelemetrySpan scope = _tracer.StartActiveSpan("consistency_check.build_store").SetAttribute("resource.name", $"{ns}.{bucket}.{buildId}").SetAttribute("operation.name", "consistency_check.build_store");

					BuildRecord? record = await _buildStore.GetBuildAsync(ns, bucket, buildId);
					if (record == null)
					{
						_logger.LogWarning("Partial build {BuildId} in bucket {Bucket} and namespace {Namespace} .", buildId, bucket, ns);

						Interlocked.Increment(ref countOfIncorrectBuildsFound);
						if (_settings.CurrentValue.AllowDeletesInBuildStore)
						{
							await _buildStore.DeleteBuild(ns, bucket, buildId);
						}
					}
				}
			);

			_logger.LogInformation("Build Store Consistency check finished, found {CountOfIncorrectBuilds} incorrect builds. Processed {CountOfBuilds} builds.", countOfIncorrectBuildsFound, countOfBuildsChecked);
		}

		protected override Task OnStopping(BuildStoreConsistencyState state)
		{
			return Task.CompletedTask;
		}
	}
}

// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Net;
using System.Net.Http;
using System.Text;
using System.Text.Json;
using System.Threading.Tasks;
using Amazon.S3;
using Amazon.S3.Util;
using Cassandra;
using EpicGames.Horde.Storage;
using Jupiter.Common;
using Jupiter.Controllers;
using Jupiter.Implementation;
using Microsoft.AspNetCore.Hosting;
using Microsoft.AspNetCore.Mvc;
using Microsoft.AspNetCore.TestHost;
using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.VisualStudio.TestTools.UnitTesting;
using Moq;
using Moq.Contrib.HttpClient;
using Serilog;
using Logger = Serilog.Core.Logger;

namespace Jupiter.FunctionalTests.Replication
{

	[TestClass]
	[DoNotParallelize]
	public class BlobsReplicatorTests
	{
		protected NamespaceId TestNamespace { get; } = new NamespaceId("test-namespace");
		protected const string TestStoragePool = "test";
		protected const string TestRegion = "test-peer";
		protected const string TestBucketName = "remote-bucket";

		protected TestServer? Server { get; set; } = null;
		private IBlobService BlobService { get; set; } = null!;

		private BucketId TestBucket { get; } = new BucketId("test");

		[TestInitialize]
		public async Task SetupAsync()
		{
			IConfigurationRoot configuration = new ConfigurationBuilder()
				// we are not reading the base appSettings here as we want exact control over what runs in the tests
				.AddJsonFile("appsettings.Testing.json", true)
				.AddInMemoryCollection(GetSettings())
				.AddEnvironmentVariables()
				.Build();

			Logger logger = new LoggerConfiguration()
				.ReadFrom.Configuration(configuration)
				.CreateLogger();

			TestServer server = new TestServer(new WebHostBuilder()
				.UseConfiguration(configuration)
				.UseEnvironment("Testing")
				.ConfigureServices(collection => collection.AddSerilog(logger))
				.UseStartup<JupiterStartup>()
			);
			server.CreateClient();
			Server = server;

			BlobService = Server.Services.GetService<IBlobService>()!;
			
			await OnSetupAsync();

			await Task.CompletedTask;
		}
		
		protected virtual Task OnSetupAsync() {
			return Task.CompletedTask;
		}

		protected virtual IRegionalBlobStore? GetRegionalBlobStore()
		{
			return null;
		}

		protected virtual IEnumerable<KeyValuePair<string, string?>> GetSettings()
		{
			return new[]
			{
				new KeyValuePair<string, string?>("UnrealCloudDDC:ReferencesDbImplementation", UnrealCloudDDCSettings.ReferencesDbImplementations.Scylla.ToString()),
				new KeyValuePair<string, string?>("UnrealCloudDDC:ReplicationLogWriterImplementation", UnrealCloudDDCSettings.ReplicationLogWriterImplementations.Scylla.ToString()),
			};
		}
		private static async Task TeardownDbAsync(IServiceProvider provider)
		{
			await Task.CompletedTask;
			IScyllaSessionManager scyllaSessionManager = provider.GetService<IScyllaSessionManager>()!;
			ISession session = scyllaSessionManager.GetSessionForLocalKeyspace();

			// remove blob replication log table as we expect it to be empty when starting the tests
			await session.ExecuteAsync(new SimpleStatement("DROP TABLE IF EXISTS blob_replication_log;"));
			// remove the snapshots
			await session.ExecuteAsync(new SimpleStatement("DROP TABLE IF EXISTS replication_snapshot;"));
		}

		[TestCleanup]
		public async Task TeardownAsync()
		{
			if (Server != null)
			{
				await TeardownDbAsync(Server.Services);
			}
		}

		[TestMethod]
		public async Task ReplicationIncrementalStateAsync()
		{
			ReplicatorSettings replicatorSettings = new()
			{
				ConnectionString = "http://localhost",
				MaxParallelReplications = 16,
				NamespaceToReplicate = TestNamespace.ToString(),
				ReplicatorName = "test-replicator",
				Version = ReplicatorVersion.Blobs
			};

			ClusterSettings clusterSettings = new();

			List<BlobReplicationLogEvent> replicationEvents0 = new();
			List<BlobReplicationLogEvent> replicationEvents1 = new();
			Dictionary<BlobId, byte[]> blobs = new();

			DateTime initialReplicationBucketTimestamp = DateTime.UtcNow.AddMinutes(-10);
			string replicationBucket0 = initialReplicationBucketTimestamp.ToReplicationBucket().ToReplicationBucketIdentifier();
			string previousReplicationBucket = initialReplicationBucketTimestamp.AddMinutes(-5).ToReplicationBucket().ToReplicationBucketIdentifier();
			string replicationBucket1 = initialReplicationBucketTimestamp.AddMinutes(5).ToReplicationBucket().ToReplicationBucketIdentifier();
			string nextReplicationBucket = initialReplicationBucketTimestamp.AddMinutes(10).ToReplicationBucket().ToReplicationBucketIdentifier();
			const int CountOfTestEvents = 100;
			for (int i = 0; i < CountOfTestEvents; i++)
			{
				byte[] blobContents = Encoding.UTF8.GetBytes($"random content {i}");
				BlobId blob = BlobId.FromBlob(blobContents);
				blobs.Add(blob, blobContents);
				replicationEvents0.Add(new BlobReplicationLogEvent(TestNamespace, blob, Guid.NewGuid(), replicationBucket0, DateTime.UtcNow, BlobReplicationLogEvent.OpType.Added, TestBucket));
			}

			for (int i = 0; i < CountOfTestEvents; i++)
			{
				byte[] blobContents = Encoding.UTF8.GetBytes($"random content timebucket 1 {i}");
				BlobId blob = BlobId.FromBlob(blobContents);
				blobs.Add(blob, blobContents);
				replicationEvents1.Add(new BlobReplicationLogEvent(TestNamespace, blob, Guid.NewGuid(), replicationBucket1, DateTime.UtcNow, BlobReplicationLogEvent.OpType.Added, TestBucket));
			}

			await Parallel.ForEachAsync(blobs, async (it, token) =>
			{
				// Cleanup persistent objects, if they exist
				if (await BlobService.ExistsAsync(TestNamespace, it.Key, cancellationToken: token))
				{
					await BlobService.DeleteObjectAsync(TestNamespace, it.Key, token);
				}
			});
			
			// Verify empty state before starting replication
			BlobId[] initialMissingBlobs = await BlobService.FilterOutKnownBlobsAsync(TestNamespace, blobs.Keys.ToArray());
			Assert.AreEqual(blobs.Count, initialMissingBlobs.Length);

			Mock<HttpMessageHandler> handler = new Mock<HttpMessageHandler>();
			handler.SetupRequest($"http://localhost/api/v1/replication-log/blobs/{TestNamespace}/{replicationBucket0}").ReturnsResponse(JsonSerializer.Serialize(new BlobReplicationLogEvents(replicationEvents0)), "application/json");
			handler.SetupRequest($"http://localhost/api/v1/replication-log/blobs/{TestNamespace}/{replicationBucket1}").ReturnsResponse(JsonSerializer.Serialize(new BlobReplicationLogEvents(replicationEvents1)), "application/json");
			handler.SetupRequest($"http://localhost/api/v1/replication-log/blobs/{TestNamespace}/{nextReplicationBucket}").ReturnsResponse(JsonSerializer.Serialize(new BlobReplicationLogEvents()), "application/json");

			foreach ((BlobId key, byte[] blobContent) in blobs)
			{
				handler.SetupRequest($"http://localhost/api/v1/blobs/{TestNamespace}/{key}").ReturnsResponse(blobContent, "application/octet-stream").Verifiable();
			}

			IHttpClientFactory httpClientFactory = handler.CreateClientFactory();
			using BlobsReplicator replicator = ActivatorUtilities.CreateInstance<BlobsReplicator>(Server!.Services, replicatorSettings, clusterSettings, httpClientFactory);
			replicator.SetRefState(previousReplicationBucket);
			bool didRun = await replicator.TriggerNewReplicationsAsync();

			Assert.IsTrue(didRun);

			handler.Verify();

			// Verify that the objects are present
			BlobId[] missingBlobs = await BlobService.FilterOutKnownBlobsAsync(TestNamespace, blobs.Keys.ToArray());
			Assert.IsFalse(missingBlobs.Any());
		}

		[TestMethod]
		public async Task ReplicationNamespaceMissingAsync()
		{
			ReplicatorSettings replicatorSettings = new()
			{
				ConnectionString = "http://localhost",
				MaxParallelReplications = 16,
				NamespaceToReplicate = TestNamespace.ToString(),
				ReplicatorName = "test-replicator",
				Version = ReplicatorVersion.Blobs
			};

			ClusterSettings clusterSettings = new();

			DateTime initialReplicationBucketTimestamp = DateTime.UtcNow.AddMinutes(-5);
			string replicationBucket = initialReplicationBucketTimestamp.ToReplicationBucket().ToReplicationBucketIdentifier();
			string previousReplicationBucket = initialReplicationBucketTimestamp.AddMinutes(-5).ToReplicationBucket().ToReplicationBucketIdentifier();

			Mock<HttpMessageHandler> handler = new Mock<HttpMessageHandler>();
			handler.SetupRequest($"http://localhost/api/v1/replication-log/blobs/{TestNamespace}/{replicationBucket}").ReturnsResponse(HttpStatusCode.NotFound);

			IHttpClientFactory httpClientFactory = handler.CreateClientFactory();
			using BlobsReplicator replicator = ActivatorUtilities.CreateInstance<BlobsReplicator>(Server!.Services, replicatorSettings, clusterSettings, httpClientFactory);
			replicator.SetRefState(previousReplicationBucket);

			await Assert.ThrowsExceptionAsync<NamespaceNotFoundException>( async () =>
			{
				await replicator.TriggerNewReplicationsAsync();
			});

			handler.Verify();
		}

		[TestMethod]
		public async Task ReplicationNoDataFoundAsync()
		{
			ReplicatorSettings replicatorSettings = new()
			{
				ConnectionString = "http://localhost",
				MaxParallelReplications = 16,
				NamespaceToReplicate = TestNamespace.ToString(),
				ReplicatorName = "test-replicator",
				Version = ReplicatorVersion.Blobs
			};

			ClusterSettings clusterSettings = new();

			DateTime initialReplicationBucketTimestamp = DateTime.UtcNow.AddMinutes(-5);
			string replicationBucket = initialReplicationBucketTimestamp.ToReplicationBucket().ToReplicationBucketIdentifier();
			string previousReplicationBucket = initialReplicationBucketTimestamp.AddMinutes(-5).ToReplicationBucket().ToReplicationBucketIdentifier();

			Mock<HttpMessageHandler> handler = new Mock<HttpMessageHandler>();
			handler.SetupRequest($"http://localhost/api/v1/replication-log/blobs/{TestNamespace}/{replicationBucket}").ReturnsResponse(HttpStatusCode.BadRequest, JsonSerializer.Serialize(new ProblemDetails { Type = ProblemTypes.NoDataFound }));

			IHttpClientFactory httpClientFactory = handler.CreateClientFactory();
			using BlobsReplicator replicator = ActivatorUtilities.CreateInstance<BlobsReplicator>(Server!.Services, replicatorSettings, clusterSettings, httpClientFactory);
			replicator.SetRefState(previousReplicationBucket);
			bool didRun = await replicator.TriggerNewReplicationsAsync();

			Assert.IsFalse(didRun);

			handler.Verify();
		}

		[TestMethod]
		public async Task ReplicationIncrementalStateWithPeerLookupAsync()
		{
			ReplicatorSettings replicatorSettings = new()
			{
				SourceSite = TestRegion,
				ConnectionString = "http://localhost:4321", // Should not get hit
				MaxParallelReplications = 16,
				NamespaceToReplicate = TestNamespace.ToString(),
				ReplicatorName = "test-replicator",
				Version = ReplicatorVersion.Blobs
			};

			ClusterSettings clusterSettings = new()
			{
				Peers = new List<PeerSettings> {
					new()
					{
						Name = TestRegion,
						FullName = TestRegion,
						Endpoints = new List<PeerEndpoints> {
							new()
							{
								Url = new Uri("http://localhost:1234"),
								IsInternal = true
							}
						}
					}
				}
			};

			List<BlobReplicationLogEvent> replicationEvents0 = new();
			List<BlobReplicationLogEvent> replicationEvents1 = new();
			Dictionary<BlobId, byte[]> blobs = new();

			DateTime initialReplicationBucketTimestamp = DateTime.UtcNow.AddMinutes(-10);
			string replicationBucket0 = initialReplicationBucketTimestamp.ToReplicationBucket().ToReplicationBucketIdentifier();
			string previousReplicationBucket = initialReplicationBucketTimestamp.AddMinutes(-5).ToReplicationBucket().ToReplicationBucketIdentifier();
			string replicationBucket1 = initialReplicationBucketTimestamp.AddMinutes(5).ToReplicationBucket().ToReplicationBucketIdentifier();
			string nextReplicationBucket = initialReplicationBucketTimestamp.AddMinutes(10).ToReplicationBucket().ToReplicationBucketIdentifier();
			const int CountOfTestEvents = 100;
			for (int i = 0; i < CountOfTestEvents; i++)
			{
				byte[] blobContents = Encoding.UTF8.GetBytes($"random content {i}");
				BlobId blob = BlobId.FromBlob(blobContents);
				blobs.Add(blob, blobContents);
				replicationEvents0.Add(new BlobReplicationLogEvent(TestNamespace, blob, Guid.NewGuid(), replicationBucket0, DateTime.UtcNow, BlobReplicationLogEvent.OpType.Added, TestBucket));
			}

			for (int i = 0; i < CountOfTestEvents; i++)
			{
				byte[] blobContents = Encoding.UTF8.GetBytes($"random content timebucket 1 {i}");
				BlobId blob = BlobId.FromBlob(blobContents);
				blobs.Add(blob, blobContents);
				replicationEvents1.Add(new BlobReplicationLogEvent(TestNamespace, blob, Guid.NewGuid(), replicationBucket1, DateTime.UtcNow, BlobReplicationLogEvent.OpType.Added, TestBucket));
			}

			await Parallel.ForEachAsync(blobs, async (it, token) =>
			{
				// Cleanup persistent objects, if they exist
				if (await BlobService.ExistsAsync(TestNamespace, it.Key, cancellationToken: token))
				{
					await BlobService.DeleteObjectAsync(TestNamespace, it.Key, token);
				}

				// Seed regional bucket if necessary
				if (BlobService.IsRegional())
				{
					await GetRegionalBlobStore()!.PutObjectAsync(TestNamespace, it.Value, it.Key, replicatorSettings.SourceSite);
				}
			});
			
			// Verify empty state before starting replication
			BlobId[] initialMissingBlobs = await BlobService.FilterOutKnownBlobsAsync(TestNamespace, blobs.Keys.ToArray());
			Assert.AreEqual(blobs.Count, initialMissingBlobs.Length);

			Mock<HttpMessageHandler> handler = new Mock<HttpMessageHandler>();
			
			handler.SetupRequest($"http://localhost:1234/api/v1/replication-log/blobs/{TestNamespace}/{replicationBucket0}").ReturnsResponse(JsonSerializer.Serialize(new BlobReplicationLogEvents(replicationEvents0)), "application/json");
			handler.SetupRequest($"http://localhost:1234/api/v1/replication-log/blobs/{TestNamespace}/{replicationBucket1}").ReturnsResponse(JsonSerializer.Serialize(new BlobReplicationLogEvents(replicationEvents1)), "application/json");
			handler.SetupRequest($"http://localhost:1234/api/v1/replication-log/blobs/{TestNamespace}/{nextReplicationBucket}").ReturnsResponse(JsonSerializer.Serialize(new BlobReplicationLogEvents()), "application/json");

			if (!BlobService.IsRegional())
			{
				foreach ((BlobId key, byte[] blobContent) in blobs)
				{
					handler.SetupRequest($"http://localhost:1234/api/v1/blobs/{TestNamespace}/{key}").ReturnsResponse(blobContent, "application/octet-stream").Verifiable();
				}
			}

			IHttpClientFactory httpClientFactory = handler.CreateClientFactory();
			using BlobsReplicator replicator = ActivatorUtilities.CreateInstance<BlobsReplicator>(Server!.Services, replicatorSettings, clusterSettings, httpClientFactory);
			replicator.SetRefState(previousReplicationBucket);
			bool didRun = await replicator.TriggerNewReplicationsAsync();

			Assert.IsTrue(didRun);

			handler.Verify();

			// Verify that the objects are present
			BlobId[] missingBlobs = await BlobService.FilterOutKnownBlobsAsync(TestNamespace, blobs.Keys.ToArray());
			Assert.IsFalse(missingBlobs.Any());
		}
	}
	
	[TestClass]
	public class RegionalBlobsReplicatorTests : BlobsReplicatorTests
	{
		protected override async Task OnSetupAsync()
		{
			await base.OnSetupAsync();
			
			IAmazonS3 s3 = Server!.Services.GetService<AmazonS3Factory>()!.Create(TestRegion);

			if (!await AmazonS3Util.DoesS3BucketExistV2Async(s3, TestBucketName))
			{
				await s3.EnsureBucketExistsAsync(TestBucketName);
			}
		}

		protected override IRegionalBlobStore GetRegionalBlobStore()
		{
			return Server!.Services.GetService<AmazonS3Store>()!;
		}

		protected override IEnumerable<KeyValuePair<string, string?>> GetSettings()
		{
			return new[]
			{
				new KeyValuePair<string, string?>("UnrealCloudDDC:BlobIndexImplementation", UnrealCloudDDCSettings.BlobIndexImplementations.Memory.ToString()),
				new KeyValuePair<string, string?>("UnrealCloudDDC:ContentIdStoreImplementation", UnrealCloudDDCSettings.ContentIdStoreImplementations.Memory.ToString()),
				new KeyValuePair<string, string?>("UnrealCloudDDC:ReferencesDbImplementation", UnrealCloudDDCSettings.ReferencesDbImplementations.Scylla.ToString()),
				new KeyValuePair<string, string?>("UnrealCloudDDC:ReplicationLogWriterImplementation", UnrealCloudDDCSettings.ReplicationLogWriterImplementations.Scylla.ToString()),
				new KeyValuePair<string, string?>("UnrealCloudDDC:BuildStoreImplementation", UnrealCloudDDCSettings.ReferencesDbImplementations.Memory.ToString()),
				new KeyValuePair<string, string?>("UnrealCloudDDC:StorageImplementations:0", UnrealCloudDDCSettings.StorageBackendImplementations.S3.ToString()),
				new KeyValuePair<string, string?>("S3:BucketName", TestNamespace.ToString()),
				new KeyValuePair<string, string?>($"S3:SiteBucketsPerStoragePool:{TestStoragePool}:{TestRegion}", TestBucketName)
			};
		}
	}
}

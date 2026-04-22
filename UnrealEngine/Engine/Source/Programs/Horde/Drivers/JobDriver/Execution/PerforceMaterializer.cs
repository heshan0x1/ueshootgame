// Copyright Epic Games, Inc. All Rights Reserved.

using System.Diagnostics;
using System.Text.Json;
using EpicGames.Core;
using EpicGames.Perforce;
using HordeCommon.Rpc.Messages;
using JobDriver.Utility;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;
using OpenTelemetry.Trace;

namespace JobDriver.Execution;

/// <summary>
/// Options for <see cref="PerforceMaterializer" />
/// </summary>
/// <param name="DirPath">Base directory for this workspace</param>
/// <param name="AgentWorkspace">Workspace options</param>
/// <param name="HostName">Optional hostname override</param>
/// <param name="EnvVars">Optional environment variables to specify</param>
/// <param name="SyncThreads">Number of threads to use when syncing a Perforce workspace</param>
/// <param name="SyncBatch">How many files in each sync batch</param>
/// <param name="SyncBatchSize">Size of each sync batch</param>
public record PerforceMaterializerOptions(
	string DirPath,
	RpcAgentWorkspace AgentWorkspace,
	string? HostName = null,
	IReadOnlyDictionary<string, string>? EnvVars = null,
	int SyncThreads = -1,
	int SyncBatch = -1,
	int SyncBatchSize = -1);

/// <summary>
/// Materializer using standard Perforce client for syncing files
/// Main use-case are for agents using only a single stream.
/// For example, incremental build agents, as stream switching has not been optimized.
/// </summary>
public sealed class PerforceMaterializer : IWorkspaceMaterializer
{
	/// <summary>
	/// Name of this materializer
	/// </summary>
	public const string TypeName = "Perforce";
	
	internal enum TransactionStatus { Clean = 0, Dirty = 1 }
	internal record State(
		int Version,
		TransactionStatus Status,
		string Client,
		string Identifier,
		string Stream,
		int Changelist,
		int ShelvedChangelist);
	
    /// <inheritdoc/>
	public DirectoryReference DirectoryPath { get; }

    /// <inheritdoc/>
	public string Name => TypeName;

	/// <inheritdoc/>
	public string Identifier { get; }
	
	/// <inheritdoc/>
	public IReadOnlyDictionary<string, string> EnvironmentVariables { get; }
	
	/// <inheritdoc/>
	public bool IsPerforceWorkspace { get; } = true;
	
	private static readonly JsonSerializerOptions s_jsonOptions = new() { WriteIndented = true };
	private readonly PerforceMaterializerOptions _options;
	private readonly Tracer _tracer;
	private readonly ILogger _logger;
	private readonly DirectoryReference _metadataDir;
	private readonly DirectoryReference _workspaceDir;
	private PerforceSettings? _perforceSettings;
	private IPerforceConnection? _perforceWithoutClient;
	private IPerforceConnection? _perforceWithClient;
	private const string StateFilename = "State.json";
	private readonly string _stateFile;
	private readonly string _stateTempFile;

	/// <summary>
	/// Constructor
	/// </summary>
	public PerforceMaterializer(PerforceMaterializerOptions options, Tracer tracer, ILogger logger)
	{
		_options = options.SyncThreads == -1 ? options with { SyncThreads = GetDefaultThreadCount(6) } : options;
		Identifier = options.AgentWorkspace.Identifier;
		EnvironmentVariables = options.EnvVars ?? new Dictionary<string, string>();
		_tracer = tracer;
		_logger = logger;
		_metadataDir = DirectoryReference.Combine(new DirectoryReference(_options.DirPath), Identifier);
		_workspaceDir = DirectoryReference.Combine(_metadataDir, "Sync");
		_stateFile = Path.Join(_metadataDir.FullName, StateFilename);
		_stateTempFile = Path.Join(_metadataDir.FullName, StateFilename + ".tmp");
		DirectoryPath = _workspaceDir;
	}
	
	/// <inheritdoc/>
	public void Dispose()
	{
		_perforceWithClient?.Dispose();
		_perforceWithoutClient?.Dispose();
	}
	
	private async Task InitializeAsync(CancellationToken cancellationToken)
	{
		using TelemetrySpan span = _tracer.StartActiveSpan($"{nameof(PerforceMaterializer)}.{nameof(InitializeAsync)}");
		using ILoggerProgress status = _logger.BeginProgressScope("Initializing...");
		Stopwatch timer = Stopwatch.StartNew();
		
		const bool PreferNative = true;
		RpcAgentWorkspace raw = _options.AgentWorkspace;
		PerforceSettings tempSettings = new(raw.ServerAndPort, raw.UserName) { PreferNativeClient = PreferNative, Password = raw.Ticket };
		_perforceWithoutClient = await PerforceConnection.CreateAsync(tempSettings, _logger);
		await _perforceWithoutClient.LoginAsync(raw.Password, cancellationToken);
		
		// Get the host name, and fill in any missing metadata about the connection
		InfoRecord info = await _perforceWithoutClient.GetInfoAsync(InfoOptions.ShortOutput, cancellationToken);
		
		string? hostName = info.ClientHost;
		if (hostName == null)
		{
			throw new Exception("Unable to determine Perforce host name");
		}
		
		// Replace invalid characters in the workspace identifier with a '+' character
		// Append the slot index, if it's non-zero
		//			my $slot_idx = $optional_arguments->{'slot_idx'} || 0;
		//			$workspace_identifier .= sprintf("+%02d", $slot_idx) if $slot_idx;
		// If running on an edge server, append the server ID to the client name
		string edgeSuffix = String.Empty;
		if (info is { Services: not null, ServerId: not null })
		{
			string[] services = info.Services.Split(' ', StringSplitOptions.RemoveEmptyEntries);
			if (services.Any(x => x.Equals("edge-server", StringComparison.OrdinalIgnoreCase)))
			{
				edgeSuffix = $"+{info.ServerId}";
			}
		}
		
		string clientName = $"Horde+PM+{WorkspaceInfo.GetNormalizedHostName(hostName)}+{raw.Identifier}{edgeSuffix}";
		_perforceSettings = new PerforceSettings(_perforceWithoutClient.Settings) { ClientName = clientName, PreferNativeClient = PreferNative };
		status.Progress = $"({timer.Elapsed.TotalSeconds:0.0}s)";
	}
	
	/// <inheritdoc/>
	public async Task SyncAsync(int changeNum, int shelveChangeNum, SyncOptions options, CancellationToken cancellationToken)
	{
		using TelemetrySpan span = _tracer.StartActiveSpan($"{nameof(PerforceMaterializer)}.{nameof(SyncAsync)}");
		
		await InitializeAsync(cancellationToken);
		if (_perforceSettings == null || _perforceWithoutClient == null)
		{
			throw new WorkspaceMaterializationException("Perforce settings/client not initialized!");
		}
		
		State? state = await LoadStateAsync(cancellationToken);
		
		_logger.LogInformation("Status: {Status}\nClient: {PrevClient} -> {Client}\nIdentifier: {PrevId} -> {Id}\nStream: {PrevStream} -> {Stream}\nCL: {PrevCl} -> {Cl}\nShelve: {PrevShelve} -> {Shelve}",
			state?.Status,
			state?.Client, _perforceSettings.ClientName,
			state?.Identifier, Identifier,
			state?.Stream, _options.AgentWorkspace.Stream,
			state?.Changelist, changeNum,
			state?.ShelvedChangelist, shelveChangeNum);
		
		bool mustRecreateClient = state == null || state.Status == TransactionStatus.Dirty || state.Identifier != Identifier || state.Stream != _options.AgentWorkspace.Stream;
		if (mustRecreateClient)
		{
			_logger.LogInformation("Re-creating Perforce client...");
			using TelemetrySpan deleteSpan = _tracer.StartActiveSpan($"{nameof(PerforceMaterializer)}.{nameof(SyncAsync)}.Delete");
			using ILoggerProgress status = _logger.BeginProgressScope("Deleting local workspace files...");
			Stopwatch deleteTimer = Stopwatch.StartNew();
			FileUtils.ForceDeleteDirectoryContents(_workspaceDir);
			status.Progress = $"({deleteTimer.Elapsed.TotalSeconds:0.0}s)";
		}

		ClientRecord client = await CreateOrUpdateClientAsync(mustRecreateClient, cancellationToken);
		
		_logger.LogInformation("Using client {ClientName} (Host: {HostName}, Stream: {StreamName}, Type: {Type}, Root: {Path})",
			client.Name, client.Host, client.Stream, client.Type, client.Root);

		_perforceWithClient = await PerforceConnection.CreateAsync(_perforceSettings, _logger);
		await RevertInternalAsync(_perforceWithClient, cancellationToken);

		if (state != null && !mustRecreateClient)
		{
			using TelemetrySpan deleteSpan = _tracer.StartActiveSpan($"{nameof(PerforceMaterializer)}.{nameof(SyncAsync)}.Flush");
			using ILoggerProgress status = _logger.BeginProgressScope($"Flushing client to CL {state.Changelist} (p4 sync -k)");
			Stopwatch flushTimer = Stopwatch.StartNew();
			await _perforceWithClient.SyncQuietAsync(EpicGames.Perforce.SyncOptions.KeepWorkspaceFiles, -1, $"@{state.Changelist}", cancellationToken);
			status.Progress = $"({flushTimer.Elapsed.TotalSeconds:0.0}s)";
		}
		
		await SaveStateAsync(TransactionStatus.Dirty, client.Name, changeNum, shelveChangeNum, cancellationToken);
		int reportedSyncChangeNum;
		
		{
			using TelemetrySpan syncSpan = _tracer.StartActiveSpan($"{nameof(PerforceMaterializer)}.{nameof(SyncAsync)}.Sync");
			using ILoggerProgress status = _logger.BeginProgressScope("Syncing files...");
			Stopwatch syncTimer = Stopwatch.StartNew();
			
			string changeNumStr = changeNum == IWorkspaceMaterializer.LatestChangeNumber ? "#head" : "@" + changeNum;
			string shelveStr = shelveChangeNum > 0 ? $" with shelved CL {shelveChangeNum}" : "";
			_logger.LogInformation("Syncing CL {ChangeNum}{Shelve} ... (Threads: {Threads} Batch: {Batch} BatchSize: {BatchSize})",
				changeNumStr, shelveStr, _options.SyncThreads, _options.SyncBatch, _options.SyncBatchSize);

			List<SyncSummaryRecord> syncSummaries = await _perforceWithClient.SyncQuietAsync(
				EpicGames.Perforce.SyncOptions.None, -1, _options.SyncThreads, _options.SyncBatch, _options.SyncBatchSize,
				-1, -1, changeNumStr, cancellationToken);

			// The sync summary will only report the correct CL if #head was used, and we only need it in that case.
			// Otherwise, just use the explicit change number passed as the parameter.
			reportedSyncChangeNum = changeNum == IWorkspaceMaterializer.LatestChangeNumber ? syncSummaries.Max(x => x.Change) : changeNum;

			long totalFileCount = syncSummaries.Sum(x => x.TotalFileCount);
			long totalFileSize = syncSummaries.Sum(x => x.TotalFileSize);
			double totalFileSizeMb = totalFileSize / (1024.0 * 1024.0);
			_logger.LogInformation("Synced {FileCount} files. {FileSizeMb:F2} MB ({FileSize} bytes)", totalFileCount, totalFileSizeMb, totalFileSize);
			syncSpan.SetAttribute("horde.pm.file_count", totalFileCount);
			syncSpan.SetAttribute("horde.pm.file_size", totalFileSize);
			status.Progress = $"({syncTimer.Elapsed.TotalSeconds:0.0}s)";
		}

		if (shelveChangeNum > 0)
		{
			using TelemetrySpan unshelveSpan = _tracer.StartActiveSpan($"{nameof(PerforceMaterializer)}.{nameof(SyncAsync)}.Unshelve");
			using ILoggerProgress status = _logger.BeginProgressScope("Unshelving files...");
			Stopwatch unshelveTimer = Stopwatch.StartNew();
			
			List<UnshelveRecord> unshelveRecords = await _perforceWithClient.UnshelveAsync(shelveChangeNum, -1, null, null, null, UnshelveOptions.ForceOverwrite, Array.Empty<string>(), cancellationToken);

			unshelveSpan.SetAttribute("horde.pm.file_count", unshelveRecords.Count);
			_logger.LogInformation("Unshelved {FileCount} files.", unshelveRecords.Count);
			status.Progress = $"({unshelveTimer.Elapsed.TotalSeconds:0.0}s)";
		}

		await SaveStateAsync(TransactionStatus.Clean, client.Name, reportedSyncChangeNum, shelveChangeNum, cancellationToken);
	}
	
	/// <inheritdoc/>
	public async Task FinalizeAsync(CancellationToken cancellationToken)
	{
		if (_perforceWithClient != null)
		{
			await RevertInternalAsync(_perforceWithClient, cancellationToken);
		}
	}

	private async Task RevertInternalAsync(IPerforceConnection perforce, CancellationToken cancellationToken)
	{
		using TelemetrySpan span = _tracer.StartActiveSpan($"{nameof(PerforceMaterializer)}.{nameof(RevertInternalAsync)}");
		using ILoggerProgress status = _logger.BeginProgressScope("Reverting files...");
		Stopwatch timer = Stopwatch.StartNew();

		List<RevertRecord> revertRecords = await perforce.RevertAsync(-1, null, RevertOptions.DeleteAddedFiles, "//...", cancellationToken);
		_logger.LogInformation("Reverted {FileCount} files.", revertRecords.Count);
		
		span.SetAttribute("horde.pm.num_files", revertRecords.Count);
		status.Progress = $"({timer.Elapsed.TotalSeconds:0.0}s)";
	}
	
	private async Task<ClientRecord> CreateOrUpdateClientAsync(bool forceCreate, CancellationToken cancellationToken)
	{
		using TelemetrySpan span = _tracer.StartActiveSpan($"{nameof(PerforceMaterializer)}.{nameof(CreateOrUpdateClientAsync)}");
		if (_perforceSettings == null || _perforceWithoutClient == null)
		{
			throw new WorkspaceMaterializationException("Perforce settings/client not initialized!");
		}
		
		string clientName = _perforceSettings.ClientName!;
		string streamName = _options.AgentWorkspace.Stream;
		
		ClientRecord newClient = new (clientName, _perforceSettings.UserName, _workspaceDir.FullName)
		{
			Host = _options.HostName ?? Environment.MachineName,
			Stream = streamName,
			Type = "partitioned",
			Root = _workspaceDir.FullName,
		};
		Directory.CreateDirectory(newClient.Root);
		
		using ILoggerProgress status = _logger.BeginProgressScope(forceCreate ? "Force creating client ..." : "Updating client ...");
		Stopwatch timer = Stopwatch.StartNew();
		
		if (forceCreate)
		{
			await _perforceWithoutClient.TryDeleteClientAsync(DeleteClientOptions.None, clientName, cancellationToken);
			await _perforceWithoutClient.CreateClientAsync(newClient, cancellationToken);
		}
		else
		{
			// Create or update the client
			PerforceResponse createRes = await _perforceWithoutClient.TryCreateClientAsync(newClient, cancellationToken);
			if (!createRes.Succeeded)
			{
				_logger.LogInformation("Deleting and creating client...");
				await _perforceWithoutClient.TryDeleteClientAsync(DeleteClientOptions.None, clientName, cancellationToken);
				await _perforceWithoutClient.CreateClientAsync(newClient, cancellationToken);
			}
		}
		
		status.Progress = $"({timer.Elapsed.TotalSeconds:0.0}s)";
		return newClient;
	}

	private Task SaveStateAsync(TransactionStatus status, string client ,int changelist, int shelvedChangelist, CancellationToken cancellationToken)
	{
		State pms = new (1, status, client, Identifier, _options.AgentWorkspace.Stream, changelist, shelvedChangelist);
		return SaveStateAsync(pms, cancellationToken);
	}
	
	private async Task SaveStateAsync(State state, CancellationToken cancellationToken)
	{
		try
		{
			string json = JsonSerializer.Serialize(state, s_jsonOptions);
			await File.WriteAllTextAsync(_stateTempFile, json, cancellationToken);
			File.Move(_stateTempFile, _stateFile, overwrite: true);
		}
		catch (Exception e)
		{
			throw new WorkspaceMaterializationException("Failed to save local workspace state", e);
		}
	}
	
	private async Task<State?> LoadStateAsync(CancellationToken cancellationToken)
	{
		try
		{
			if (!File.Exists(_stateFile))
			{
				return null;
			}

			string json = await File.ReadAllTextAsync(_stateFile, cancellationToken);
			return JsonSerializer.Deserialize<State>(json, s_jsonOptions);
		}
		catch (Exception e)
		{
			_logger.LogWarning(e, "Failed to load local workspace state");
			return null;
		}
	}
	
	private static int GetDefaultThreadCount(int defaultValue) => Math.Min(defaultValue, Math.Max(Environment.ProcessorCount - 1, 1));
	
	internal async Task DeleteClientForTestAsync(CancellationToken cancellationToken)
	{
		if (_perforceSettings == null || _perforceWithoutClient == null)
		{
			throw new WorkspaceMaterializationException("Perforce settings/client not initialized!");
		}
		await _perforceWithoutClient.DeleteClientAsync(DeleteClientOptions.None, _perforceSettings.ClientName!, cancellationToken);
	}
	
	internal Task<State?> LoadStateForTestAsync(CancellationToken cancellationToken)
	{
		return LoadStateAsync(cancellationToken);
	}
}

class PerforceMaterializerFactory(IServiceProvider serviceProvider) : IWorkspaceMaterializerFactory
{
	/// <inheritdoc/>
	public Task<IWorkspaceMaterializer?> CreateMaterializerAsync(string name, RpcAgentWorkspace workspaceInfo, DirectoryReference workspaceDir, bool forAutoSdk, CancellationToken cancellationToken)
	{
		if (name.Equals(PerforceMaterializer.TypeName, StringComparison.OrdinalIgnoreCase))
		{
			Tracer tracer = serviceProvider.GetRequiredService<Tracer>();
			ILogger<PerforceMaterializer> logger = serviceProvider.GetRequiredService<ILogger<PerforceMaterializer>>();
			PerforceMaterializerOptions options = new (workspaceDir.FullName, workspaceInfo);
			return Task.FromResult<IWorkspaceMaterializer?>(new PerforceMaterializer(options, tracer, logger));
		}
		return Task.FromResult<IWorkspaceMaterializer?>(null);
	}
}
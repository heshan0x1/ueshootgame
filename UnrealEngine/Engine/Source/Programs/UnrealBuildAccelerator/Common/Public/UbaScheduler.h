// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UbaMemory.h"
#include "UbaProcessStartInfo.h"
#include "UbaThread.h"

namespace uba
{
	class CacheClient;
	class Config;
	class ConfigTable;
	class Process;
	class ProcessImpl;
	class RootPaths;
	class SessionServer;
	class WorkManagerImpl;
	struct NextProcessInfo;
	struct ProcessStartInfoHolder;

	////////////////////////////////////////////////////////////////////////////////////////////////////

	struct SchedulerCreateInfo
	{
		SchedulerCreateInfo(SessionServer& s) : session(s) {}

		void Apply(const Config& config);

		SessionServer& session;
		CacheClient** cacheClients = nullptr; // Set cache clients for scheduler to use when building
		u32 cacheClientCount = 0;
		u32 maxLocalProcessors = ~0u; // Max local processors to use. ~0u means it will use all processors
		u32 maxParallelCacheQueries = 16u; // Max number of parallel cache queries allowed
		bool enableProcessReuse = false; // If this is true, the system will allow processes to be reused when they're asking for it.
		bool forceRemote = false; // Force all processes that can run remotely to run remotely.
		bool forceNative = false; // Force all processes to run native (not detoured)
		bool writeToCache = false; // Set to true in combination with setting cacheClient to populate cache
		bool useThreadToExitRemoteProcess = false; // Set this to true to move exit process call from job to thread. 
		bool memWatchdog = false; // Will watch memory and throttle/kill processes if needed
		u8 memTracingLevel = 0;
		u8 memStartWaitPercent = 85; // When memory usage goes above this percent, no new processes will be spawned until back below
		u8 memStartKillPercent = 95; // When memory usage goes above this percent, newest processes will be killed to bring it back below
		u32 memPagefileActivityThreshold = 0; // When page file activity goes over this number between each watchdog update we start killing processes. 0=disabled
		ConfigTable* processConfigs = nullptr;
	};

	////////////////////////////////////////////////////////////////////////////////////////////////////

	struct EnqueueProcessInfo
	{
		EnqueueProcessInfo(const ProcessStartInfo& i) : info(i) {}

		const ProcessStartInfo& info;
		
		float weight = 1.0f; // Weight of process. This is used towards max local processors. If a process is multithreaded it is likely it's weight should be more than 1.0
		bool canDetour = true; // If true, uba will detour the process. If false it will just create pipes for std out and then run the process as-is.
		bool canExecuteRemotely = true; // If true, this process can run on other machines, if false it will always be executed locally

		const void* knownInputs = nullptr; // knownInputs is a memory block with null terminated tchar strings followed by an empty null terminated string to end.
		u32 knownInputsBytes = 0; // knownInputsBytes is the total size in bytes of knownInputs
		u32 knownInputsCount = 0; // knownInputsCount is the number of strings in the memory block

		const u32* dependencies = nullptr; // An array of u32 holding indicies to processes this process depends on. Index is a rolling number returned by EnqueueProcess
		u32 dependencyCount = 0; // Number of elements in dependencies

		u32 cacheBucketId = 0; // Bucket that cache should be fetched from. Zero means that it will not fetch anything
		u32 memoryGroupId = 0; // Group that calculates average of memory usage.

		u64 predictedMemoryUsage = 0; // If zero, this will not be used for memory predictions
	};

	////////////////////////////////////////////////////////////////////////////////////////////////////

	class Scheduler
	{
	public:
		Scheduler(const SchedulerCreateInfo& info);
		~Scheduler();

		void Start(); // Start scheduler thread. Should be called before server starts listen to connections if using remote help
		void Stop(); // Will wait on all active processes and then exit.
		void Cancel(); // Cancel and wait
		void SetMaxLocalProcessors(u32 maxLocalProcessors); // Set max local processes
		void SetAllowDisableRemoteExecution(bool allow); // Allow scheduler to tell clients to disconnect early if running out of processes

		u32 EnqueueProcess(const EnqueueProcessInfo& info); // Returns index of process. Index is a rolling number

		void GetStats(u32& outQueued, u32& outActiveLocal, u32& outActiveRemote, u32& outFinished); // This is not "threadsafe".. which means that you are not guaranteed to get the exact correct state.

		bool IsEmpty(); // Returns true if scheduler is entirely empty.. as in no processes are left in the system

		bool EnqueueFromFile(const tchar* yamlFilename, const Function<void(EnqueueProcessInfo&)>& enqueued = {}); // Enqueue actions from file. Example of format of file is at the end of this file
		bool EnqueueFromSpecialJson(const tchar* jsonFilename, const tchar* workingDir, const tchar* description, RootsHandle rootsHandle, void* userData = nullptr);

		void SetProcessFinishedCallback(const Function<void(const ProcessHandle&)>& processFinished); // Set callback 

		SessionServer& GetSession() { return m_session; }
		
		u32 GetProcessCountThatCanRunRemotelyNow();
		float GetProcessWeightThatCanRunRemotelyNow();

	private:
		struct CacheFetchInfo;
		struct ExitProcessInfo;
		struct ProcessStartInfo2;

		enum RunStatus : u8
		{
			RunStatus_QueuedForRun,
			RunStatus_Running,
			RunStatus_Success,
			RunStatus_Failed,
			RunStatus_Skipped,
		};

		enum CacheStatus : u8
		{
			CacheStatus_QueuedForTest,
			CacheStatus_Testing,
			CacheStatus_QueuedForDownload,
			CacheStatus_Downloading,
			CacheStatus_Success,
			CacheStatus_Failed,
		};

		UBA_NOINLINE void ThreadLoop();

		void SkipAllQueued();
		void Cleanup();
		bool ProcessReturned(Process& process, bool isLocal);
		void HandleCacheMissed(u32 processIndex);
		void RemoteSlotAvailable(bool isCrossArchitecture);
		void ProcessExited(ExitProcessInfo* info, const ProcessHandle& handle);
		bool CanRun();
		u32 PopCache();
		u32 PopProcess(bool isLocal, RunStatus& outPrevStatus);
		bool RunCacheQuery();
		bool RunQueuedProcess(bool isLocal);
		bool HandleReuseMessage(Process& process, NextProcessInfo& outNextProcess, u32 prevExitCode);
		void ExitProcess(ExitProcessInfo& info, Process& process, u32 exitCode, bool fromCache);
		void SkipProcess(ProcessStartInfo2& info);
		void UpdateStatusHitMissCount();
		void UpdateQueueCounter(int offset);
		void UpdateActiveProcessCounter(bool isLocal, int offset);
		void FinishProcess(const ProcessHandle& handle);

		void KillNewestLocalProcess(StringView reason);
		void NativeProcessCreated(ProcessImpl& process);
		void LocalProcessStart(ProcessStartInfo2& info);
		bool LocalProcessOkToSpawn();
		void LocalProcessExit(ProcessStartInfo2& info, const ProcessHandle& handle);
		void LoadMemTrackTable();
		void SaveMemTrackTable();
		UBA_NOINLINE void ThreadMemoryCheckLoop();


		SessionServer& m_session;
		u32 m_maxLocalProcessors;
		u32 m_maxParallelCacheQueries;
		
		struct ProcessEntry
		{
			ProcessStartInfo2* info;
			u32* dependencies;
			u32 dependencyCount;
			CacheStatus cacheStatus;
			RunStatus runStatus;
			bool canDetour;
			bool canExecuteRemotely;
		};

		Futex m_processEntriesLock;
		Vector<ProcessEntry> m_processEntries;

		Function<void(const ProcessHandle&)> m_processFinished;

		Event m_updateThreadWakeup;
		Thread m_updateThread;
		u32 m_processEntriesRunStart = 0;

		u32 m_processEntriesCacheStart = 0;

		Atomic<bool> m_loop;
		bool m_enableProcessReuse;
		bool m_forceRemote;
		bool m_forceNative;
		bool m_useThreadToExitRemoteProcess;
		bool m_allowDisableRemoteExecution = false;
		bool m_cancelled = false;
		ConfigTable* m_processConfigs = nullptr;

		float m_activeLocalProcessWeight = 0.0f;
		u32 m_activeCacheQueries = 0;
		Atomic<u32> m_activeCacheDownloads;

		Atomic<u32> m_totalProcesses;
		Atomic<u32> m_queuedProcesses;
		Atomic<u32> m_activeLocalProcesses;
		Atomic<u32> m_activeRemoteProcesses;
		Atomic<u32> m_finishedProcesses;
		Atomic<u32> m_errorCount;
		Atomic<u32> m_cacheHitCount;
		Atomic<u32> m_cacheMissCount;

		Vector<CacheClient*> m_cacheClients;
		Vector<RootPaths*> m_rootPaths;
		bool m_writeToCache;

		WorkManagerImpl* m_cacheWorkManager = nullptr;
		List<Thread> m_finishedRemoteProcessThreads;

		CriticalSection m_wasReturnedLock;

		bool m_memWatchdog = false;
		Event m_memThreadEvent;
		Thread m_memThread;
		bool m_memDelaySpawning = false;
		Atomic<u64> m_memEstimatedUsage;
		Atomic<u64> m_memWaitThreshold;
		u8 m_memStartWaitPercent = 0;
		u8 m_memStartKillPercent = 0;
		u8 m_memTracingLevel = 0;
		u32 m_memPagefileActivityThreshold = 0;

		struct MemGroupStats;
		struct MemGroupEntry;
		Futex m_memGroupLookupLock;
		UnorderedMap<u32, MemGroupStats> m_memGroupLookup;
		UnorderedMap<u32, MemGroupEntry> m_memGroupDb;

		Futex m_processesLock;
		struct ProcessRecord { ProcessImpl& process; ExitProcessInfo* ep; };
		UnorderedMap<StringKey, ProcessRecord> m_processes;

		Scheduler(const Scheduler&) = delete;
		void operator=(const Scheduler&) = delete;
	};

	////////////////////////////////////////////////////////////////////////////////////////////////////
}

#if 0
// Example of yaml file with processes that can be queued up in scheduler
// id - Not used right now. Number does not matter because id will be a rolling number
// app - Application to execute
// arg - Arguments to application
// dir - Working directory
// desc - Description of process
// weight - How much cpu this process is using. Optional. Defaults to 1.0
// remote - Decides if this process can execute remotely. Optional. Defaults to true
// detour - Decides if this process can be detoured. Optional. Defaults to true.
// dep - Dependencies. An array of indices to other processes

processes:
  - id: 0
    app: C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Tools\MSVC\14.38.33130\bin\Hostx64\x64\cl.exe
    arg: @"..\Intermediate\Build\Win64\x64\UnrealPak\Development\Core\SharedPCH.Core.Cpp20.h.obj.rsp"
    dir: E:\dev\fn\Engine\Source
    desc: SharedPCH.Core.Cpp20.cpp
    weight: 1.25
    remote: false

  - id: 44
    app: C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Tools\MSVC\14.38.33130\bin\Hostx64\x64\cl.exe
    arg: @"..\Intermediate\Build\Win64\x64\UnrealPak\Development\Json\Module.Json.cpp.obj.rsp"
    dir: E:\dev\fn\Engine\Source
    desc: Module.Json.cpp
    weight: 1.5
    dep: [0]

  - id: 337
    app: E:\dev\fn\Engine\Binaries\ThirdParty\DotNet\6.0.302\windows\dotnet.exe
    arg: "E:\dev\fn\Engine\Binaries\DotNET\UnrealBuildTool\UnrealBuildTool.dll" -Mode=WriteMetadata -Input="E:\dev\fn\Engine\Intermediate\Build\Win64\x64\UnrealPak\Development\TargetMetadata.dat" -Version=2
    dir: E:\dev\fn\Engine\Source
    desc: UnrealPak.target
    detour: false
    dep: [336, 0]

#endif
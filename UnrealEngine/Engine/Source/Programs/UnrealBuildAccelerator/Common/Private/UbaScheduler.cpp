// Copyright Epic Games, Inc. All Rights Reserved.

#include "UbaScheduler.h"
#include "UbaApplicationRules.h"
#include "UbaCacheClient.h"
#include "UbaConfig.h"
#include "UbaEnvironment.h"
#include "UbaNetworkMessage.h"
#include "UbaNetworkServer.h"
#include "UbaProcess.h"
#include "UbaProcessStartInfoHolder.h"
#include "UbaRootPaths.h"
#include "UbaSessionServer.h"
#include "UbaStringBuffer.h"

#if PLATFORM_WINDOWS
#include <winternl.h>
#endif

// TODO. This is how we want scheduler to work.
// 
// 1. Cache should be queried from front of queue.
//    We need to split up cache in fetching cache entry and fetching cas.
//    .. if there are 10000 actions and the last is a cache miss, then we want to find this out as early as possible.
// 2. Local processing should prioritize cache misses first, then query from the back of they queue
// 
// At some point cached processes and local will race which is fine.
//

namespace uba
{
	////////////////////////////////////////////////////////////////////////////////////////////////////

	#if PLATFORM_WINDOWS
	constexpr u32 CreateTimeOffset = 32;
	constexpr u32 PidOffset = 80;
	constexpr u32 ParentPidOffset = 88;
	constexpr u32 PagefileUsageOffset = 184;
	static_assert(offsetof(SYSTEM_PROCESS_INFORMATION, UniqueProcessId) == PidOffset);
	//static_assert(offsetof(SYSTEM_PROCESS_INFORMATION, InheritedFromUniqueProcessId) == ParentPidOffset);
	//static_assert(offsetof(SYSTEM_PROCESS_INFORMATION, CreateTime) == CreateTimeOffset);
	//static_assert(offsetof(SYSTEM_PROCESS_INFORMATION, PagefileUsage) == PagefileUsageOffset);
	#endif

	////////////////////////////////////////////////////////////////////////////////////////////////////

	struct Scheduler::CacheFetchInfo : CacheClient::FetchContext
	{
		CacheFetchInfo(const ProcessStartInfo& i, Session& s, RootsHandle rh, MessagePriority mp) : CacheClient::FetchContext(i, s, rh, mp) {}
		CacheResult result;
		CacheClient* client;
	};

	////////////////////////////////////////////////////////////////////////////////////////////////////

	struct Scheduler::ProcessStartInfo2 : ProcessStartInfoHolder
	{
		ProcessStartInfo2(const ProcessStartInfo& si, const u8* ki, u32 kic)
		:	ProcessStartInfoHolder(si)
		, knownInputs(ki)
		, knownInputsCount(kic)
		{
		}

		~ProcessStartInfo2()
		{
			delete[] knownInputs;
		}

		CacheFetchInfo* cacheInfo = nullptr;

		const u8* knownInputs;
		u32 knownInputsCount;
		float weight = 1.0f;
		u32 cacheBucketId = 0; // Zero means this process will not check cache
		u32 memoryGroupId = 0; // Zero means no memory group so this process will not be taken into account in memory allocations
		u64 predictedMemoryUsage = 0;
	};

	////////////////////////////////////////////////////////////////////////////////////////////////////

	struct Scheduler::ExitProcessInfo
	{
		Scheduler* scheduler = nullptr;
		ProcessStartInfo2* startInfo;
		u32 processIndex = ~0u;
		bool wasReturned = false;
		bool hasCheckedWasReturned = false;
		bool isLocal = true;
	};

	////////////////////////////////////////////////////////////////////////////////////////////////////

	struct Scheduler::MemGroupStats
	{
		u64 baseline = 0;
		u64 average = 0;
		u64 history[10] = { 0 };
		u32 historyCounter = 0;
		u32 activeProcessCount = 0;
	};

	struct Scheduler::MemGroupEntry
	{
		u64 baseline = 0;
		u64 average = 0;
	};

	////////////////////////////////////////////////////////////////////////////////////////////////////

	class SkippedProcess : public Process
	{
	public:
		SkippedProcess(const ProcessStartInfo& i) : startInfo(i) {}
		virtual u32 GetExitCode() override { return ProcessCancelExitCode; }
		virtual bool HasExited() override { return true; }
		virtual bool WaitForExit(u32 millisecondsTimeout) override{ return true; }
		virtual const ProcessStartInfo& GetStartInfo() const override { return startInfo; }
		virtual const Vector<ProcessLogLine>& GetLogLines() const override { static Vector<ProcessLogLine> v{ProcessLogLine{TC("Skipped"), LogEntryType_Warning}}; return v; }
		virtual const Vector<u8>& GetTrackedInputs() const override { static Vector<u8> v; return v;}
		virtual const Vector<u8>& GetTrackedOutputs() const override { static Vector<u8> v; return v;}
		virtual bool IsRemote() const override { return false; }
		virtual ProcessExecutionType GetExecutionType() const override { return ProcessExecutionType_Skipped; }
		ProcessStartInfoHolder startInfo;
	};

	////////////////////////////////////////////////////////////////////////////////////////////////////

	class CachedProcess : public Process
	{
	public:
		CachedProcess(const ProcessStartInfo& i) : startInfo(i) {}
		virtual u32 GetExitCode() override { return 0; }
		virtual bool HasExited() override { return true; }
		virtual bool WaitForExit(u32 millisecondsTimeout) override{ return true; }
		virtual const ProcessStartInfo& GetStartInfo() const override { return startInfo; }
		virtual const Vector<ProcessLogLine>& GetLogLines() const override { return logLines; }
		virtual const Vector<u8>& GetTrackedInputs() const override { static Vector<u8> v; return v;}
		virtual const Vector<u8>& GetTrackedOutputs() const override { static Vector<u8> v; return v;}
		virtual bool IsRemote() const override { return false; }
		virtual ProcessExecutionType GetExecutionType() const override { return ProcessExecutionType_FromCache; }
		ProcessStartInfoHolder startInfo;
		Vector<ProcessLogLine> logLines;
	};

	////////////////////////////////////////////////////////////////////////////////////////////////////

	void SchedulerCreateInfo::Apply(const Config& config)
	{
		if (const ConfigTable* table = config.GetTable(TC("Scheduler")))
		{
			table->GetValueAsBool(enableProcessReuse, TC("EnableProcessReuse"));
			table->GetValueAsBool(forceRemote, TC("ForceRemote"));
			table->GetValueAsBool(forceNative, TC("ForceNative"));
			table->GetValueAsU32(maxLocalProcessors, TC("MaxLocalProcessors"));
			table->GetValueAsU32(maxParallelCacheQueries, TC("MaxParallelCacheQueries"));
			table->GetValueAsBool(useThreadToExitRemoteProcess, TC("UseThreadToExitRemoteProcess"));
			
			table->GetValueAsBool(memWatchdog, TC("MemTrack")); // Legacy

			table->GetValueAsBool(memWatchdog, TC("MemWatchdog"));

			u32 temp = 0;
			if (table->GetValueAsU32(temp, TC("MemTracingLevel")))
				memTracingLevel = u8(temp);
			if (table->GetValueAsU32(temp, TC("MemStartWaitPercent")))
				memStartWaitPercent = u8(temp);
			if (table->GetValueAsU32(temp, TC("MemStartKillPercent")))
				memStartKillPercent = u8(temp);
			table->GetValueAsU32(memPagefileActivityThreshold, TC("MemPagefileActivityThreshold"));
		}
	}

	////////////////////////////////////////////////////////////////////////////////////////////////////

	Scheduler::Scheduler(const SchedulerCreateInfo& info)
	:	m_session(info.session)
	,	m_maxLocalProcessors(info.maxLocalProcessors != ~0u ? info.maxLocalProcessors : GetLogicalProcessorCount())
	,	m_maxParallelCacheQueries(info.maxParallelCacheQueries)
	,	m_updateThreadWakeup(false)
	,	m_enableProcessReuse(info.enableProcessReuse)
	,	m_forceRemote(info.forceRemote)
	,	m_forceNative(info.forceNative)
	,	m_useThreadToExitRemoteProcess(info.useThreadToExitRemoteProcess)
	,	m_processConfigs(info.processConfigs)
	,	m_writeToCache(info.writeToCache && info.cacheClientCount)
	{
		m_cacheClients.insert(m_cacheClients.end(), info.cacheClients, info.cacheClients + info.cacheClientCount);
		m_session.RegisterGetNextProcess([this](Process& process, NextProcessInfo& outNextProcess, u32 prevExitCode)
			{
				return HandleReuseMessage(process, outNextProcess, prevExitCode);
			});

		if (info.memWatchdog && IsWindows && !IsRunningWine()) // Only implemented for normal windows for now
		{
			m_memWatchdog = true;
			m_memTracingLevel = info.memTracingLevel;
			m_memStartWaitPercent = info.memStartWaitPercent;
			m_memStartKillPercent = info.memStartKillPercent;
			m_memPagefileActivityThreshold = info.memPagefileActivityThreshold;
		}

		m_session.SetOuterScheduler(this);
	}

	Scheduler::~Scheduler()
	{
		Stop();
		for (auto rt : m_rootPaths)
			delete rt;
	}

	void Scheduler::Start()
	{
		m_session.SetRemoteProcessReturnedEvent([this](Process& process) { ProcessReturned(process, false); });
		m_session.SetRemoteProcessSlotAvailableEvent([this](bool isCrossArchitecture) { RemoteSlotAvailable(isCrossArchitecture); });

		if (!m_cacheClients.empty())
			m_cacheWorkManager = new WorkManagerImpl(m_maxParallelCacheQueries, TC("UbaSchedCach"));

		if (m_memWatchdog)
		{
			LoadMemTrackTable();
			m_session.SetNativeProcessCreatedFunc([this](ProcessImpl& process) { NativeProcessCreated(process); });
			m_memThreadEvent.Create(true);
			m_memThread.Start([this]() { ThreadMemoryCheckLoop();  return 0; }, TC("UbaMemTrackLoop"));

			// Make sure we have an estimate before starting to spawn processes
			while (!m_memThread.Wait(1) && !m_memEstimatedUsage)
				;
		}

		m_loop = true;
		m_updateThread.Start([this]() { ThreadLoop(); return 0; }, TC("UbaSchedLoop"));
	}

	void Scheduler::Stop()
	{
		m_loop = false;
		m_cancelled = true;

		m_updateThreadWakeup.Set();
		m_updateThread.Wait();
		if (m_cacheWorkManager)
		{
			m_cacheWorkManager->FlushWork(60*1000);
			delete m_cacheWorkManager;
			m_cacheWorkManager = nullptr;
		}
		m_session.WaitOnAllTasks();
		SkipAllQueued();

		if (m_session.GetRemoteTraceEnabled())
		{
			m_session.DisableRemoteExecution();
			m_session.WaitOnAllClients();
		}

		if (m_memWatchdog)
		{
			m_memThreadEvent.Set();
			m_memThread.Wait();
			SaveMemTrackTable();
		}

		Cleanup();
	}

	void Scheduler::Cancel()
	{
		m_cancelled = true;
		m_enableProcessReuse = false;
		SkipAllQueued();
		m_session.CancelAllProcesses();
	}

	void Scheduler::SkipAllQueued()
	{
		Vector<ProcessStartInfo2*> skipped;
		SCOPED_FUTEX(m_processEntriesLock, lock);
		for (auto& entry : m_processEntries)
		{
			if (entry.runStatus == RunStatus_QueuedForRun && (entry.cacheStatus == CacheStatus_QueuedForTest || entry.cacheStatus == CacheStatus_Failed))
			{
				entry.cacheStatus = CacheStatus_Failed;
				entry.runStatus = RunStatus_Skipped;
				skipped.push_back(entry.info);
			}
		}
		lock.Leave();
		for (auto pi : skipped)
			SkipProcess(*pi);
	}

	void Scheduler::Cleanup()
	{
		SCOPED_FUTEX(m_processEntriesLock, lock);
		for (auto& entry : m_processEntries)
		{
			UBA_ASSERTF(entry.runStatus > RunStatus_Running, TC("Found processes in queue/running state when stopping scheduler."));
			delete[] entry.dependencies;
			delete entry.info;
		}
		m_processEntries.clear();
		m_processEntriesRunStart = 0;
		m_session.SetOuterScheduler(nullptr);
	}

	void Scheduler::SetMaxLocalProcessors(u32 maxLocalProcessors)
	{
		m_maxLocalProcessors = maxLocalProcessors;
		m_updateThreadWakeup.Set();
	}

	void Scheduler::SetAllowDisableRemoteExecution(bool allow)
	{
		m_allowDisableRemoteExecution = allow;
	}

	u32 Scheduler::EnqueueProcess(const EnqueueProcessInfo& info)
	{
		u8* ki = nullptr;
		if (info.knownInputsCount)
		{
			ki = new u8[info.knownInputsBytes];
			memcpy(ki, info.knownInputs, info.knownInputsBytes);
		}

		u32* dep = nullptr;
		if (info.dependencyCount)
		{
			dep = new u32[info.dependencyCount];
			memcpy(dep, info.dependencies, info.dependencyCount*sizeof(u32));
		}

		auto info2 = new ProcessStartInfo2(info.info, ki, info.knownInputsCount);
		info2->Expand();
		info2->weight = info.weight;
		info2->cacheBucketId = info.cacheBucketId;
		info2->memoryGroupId = info.memoryGroupId;
		info2->predictedMemoryUsage = info.predictedMemoryUsage;

		const ApplicationRules* rules = m_session.GetRules(*info2);
		info2->rules = rules;

		bool useCache = info.cacheBucketId && !m_cacheClients.empty() && rules->IsCacheable();
		bool canDetour = info.canDetour;
		bool canExecuteRemotely = info.canExecuteRemotely && info.canDetour;

		if (m_processConfigs)
		{
			auto name = info2->application;
			if (auto lastSeparator = TStrrchr(name, PathSeparator))
				name = lastSeparator + 1;
			StringBuffer<128> lower(name);
			lower.MakeLower();
			lower.Replace('.', '_');
			if (const ConfigTable* processConfig = m_processConfigs->GetTable(lower.data))
			{
				processConfig->GetValueAsBool(canExecuteRemotely, TC("CanExecuteRemotely"));
				processConfig->GetValueAsBool(canDetour, TC("CanDetour"));
			}
		}

		SCOPED_FUTEX(m_processEntriesLock, lock);
		u32 index = u32(m_processEntries.size());
		auto& entry = m_processEntries.emplace_back();
		entry.info = info2;
		entry.dependencies = dep;
		entry.dependencyCount = info.dependencyCount;
		entry.runStatus = RunStatus_QueuedForRun;
		entry.cacheStatus = useCache ? CacheStatus_QueuedForTest : CacheStatus_Failed;
		entry.canDetour = canDetour;
		entry.canExecuteRemotely = canExecuteRemotely;
		lock.Leave();

		++m_totalProcesses;

		UpdateQueueCounter(1);

		m_updateThreadWakeup.Set();
		return index;
	}

	void Scheduler::GetStats(u32& outQueued, u32& outActiveLocal, u32& outActiveRemote, u32& outFinished)
	{
		outActiveLocal = m_activeLocalProcesses;
		outActiveRemote = m_activeRemoteProcesses;
		outFinished = m_finishedProcesses;
		outQueued = m_queuedProcesses;
	}

	bool Scheduler::IsEmpty()
	{
		SCOPED_FUTEX_READ(m_processEntriesLock, lock);
		bool isEmpty = m_processEntries.size() <= m_finishedProcesses;
		if (isEmpty)
			return true;
			
		#if UBA_DEBUG
		if (m_cancelled)
		{
			m_session.GetLogger().Error(TC("STATUS"));
			u32 index = 0;
			for (auto& entry : m_processEntries)
			{
				if (entry.runStatus != RunStatus_Skipped && entry.runStatus != RunStatus_Failed && entry.runStatus != RunStatus_Success)
				{
					m_session.GetLogger().Error(TC("Entry %u: Run: %u Cache: %u"), index, u32(entry.runStatus), u32(entry.cacheStatus));
					break;
				}
				++index;
			}
		}
		#endif
		
		return false;
	}

	void Scheduler::SetProcessFinishedCallback(const Function<void(const ProcessHandle&)>& processFinished)
	{
		m_processFinished = processFinished;
	}

	u32 Scheduler::GetProcessCountThatCanRunRemotelyNow()
	{
		if (m_session.IsRemoteExecutionDisabled())
			return 0;

		u32 count = 0;
		SCOPED_FUTEX_READ(m_processEntriesLock, lock);
		for (auto& entry : m_processEntries)
		{
			if (!entry.canExecuteRemotely)
				continue;
			if (entry.runStatus != RunStatus_QueuedForRun || entry.cacheStatus != CacheStatus_Failed)
				continue;
			++count;
		}

		count += m_activeRemoteProcesses;

		return count;
	}

	float Scheduler::GetProcessWeightThatCanRunRemotelyNow()
	{
		if (m_session.IsRemoteExecutionDisabled())
			return 0;

		float weight = 0;
		SCOPED_FUTEX_READ(m_processEntriesLock, lock);
		for (auto& entry : m_processEntries)
		{
			if (!entry.canExecuteRemotely)
				continue;
			if (entry.runStatus != RunStatus_QueuedForRun || entry.cacheStatus != CacheStatus_Failed)
				continue;
			bool canRun = true;
			for (u32 j=0, je=entry.dependencyCount; j!=je; ++j)
			{
				auto depIndex = entry.dependencies[j];
				if (m_processEntries[depIndex].runStatus == RunStatus_Success)
					continue;
				canRun = false;
				break;
			}
			if (!canRun)
				continue;
			weight += entry.info->weight;
		}
		return weight;
	}

	void Scheduler::ThreadLoop()
	{
		while (m_loop)
		{
			if (!m_updateThreadWakeup.IsSet())
				break;

			while (RunCacheQuery())
				;

			while (RunQueuedProcess(true))
				;
		}
	}

	bool Scheduler::ProcessReturned(Process& process, bool isLocal)
	{
		auto& si = process.GetStartInfo();
		auto& ei = *(ExitProcessInfo*)si.userData;
		float processWeight = 0;
		u32 processIndex;

		if (isLocal)
		{
			if (!si.userData)
				return false;

			processWeight = ei.startInfo->weight;
			processIndex = ei.processIndex; // Must be fetched before cancel

			SCOPED_CRITICAL_SECTION(m_wasReturnedLock, lock);
			if (ei.hasCheckedWasReturned)
				return false;
			if (!process.Cancel())
				return false;
			ei.wasReturned = true;
		}
		else
		{
			processIndex = ei.processIndex; // Must be fetched before cancel
			ei.wasReturned = true;
			process.Cancel(); // Cancel will call ProcessExited
		}
		

		if (processIndex == ~0u)
		{
			UBA_ASSERT(!isLocal);
			return false;
		}

		if (isLocal)
		{
			if (!process.WaitForExit(8*60*60*1000)) // 8 hours, hopefully someone will come and get me so I can debug :)
			{
				m_updateThreadWakeup.Set();
				return m_session.GetLogger().Error(TC("Took more than 8 hours to wait for process to exit (%s)"), si.GetDescription());
			}
		}

		SCOPED_FUTEX(m_processEntriesLock, lock);

		if (isLocal)
			m_activeLocalProcessWeight -= processWeight;

		if (m_processEntries[processIndex].runStatus != RunStatus_Running)
			return m_session.GetLogger().Error(TC("This should not happen (%u)"), m_processEntries[processIndex].runStatus);

		bool success = !m_cancelled;

		if (success)
		{
			m_processEntries[processIndex].runStatus = RunStatus_QueuedForRun;
			m_processEntriesRunStart = Min(m_processEntriesRunStart, processIndex);
		}
		else
		{
			m_session.GetLogger().Error(TC("Here to check this path works"));
			m_processEntries[processIndex].runStatus = RunStatus_Skipped;
			ProcessHandle ph(&process);
			FinishProcess(ph);
		}
		lock.Leave();

		UpdateQueueCounter(1);
		UpdateActiveProcessCounter(isLocal, -1);
		m_updateThreadWakeup.Set();
		return true;
	}

	void Scheduler::HandleCacheMissed(u32 processIndex)
	{
		SCOPED_FUTEX(m_processEntriesLock, lock);
		auto& entry = m_processEntries[processIndex];
		UBA_ASSERTF((entry.cacheStatus == CacheStatus_Testing || entry.cacheStatus == CacheStatus_Downloading) && entry.runStatus == RunStatus_QueuedForRun, TC("Unexpected entry state. Run: %u Cache: %u"), u32(entry.runStatus), u32(entry.cacheStatus));

		--m_activeCacheQueries;

		entry.cacheStatus = CacheStatus_Failed;

		if (m_cancelled)
		{
			entry.runStatus = RunStatus_Skipped;
			auto pi = entry.info;
			lock.Leave();
			SkipProcess(*pi);
			return;
		}

		m_processEntriesRunStart = Min(m_processEntriesRunStart, processIndex);
		lock.Leave();

		UpdateQueueCounter(1);
		UpdateActiveProcessCounter(false, -1);
		m_updateThreadWakeup.Set();
	}

	void Scheduler::RemoteSlotAvailable(bool isCrossArchitecture)
	{
		UBA_ASSERTF(!isCrossArchitecture, TC("Cross architecture code path not implemented"));
		if (RunQueuedProcess(false))
			return;
		if (!m_allowDisableRemoteExecution)
			return;
		if (m_session.IsRemoteExecutionDisabled())
			return;
		u32 count = 0;
		SCOPED_FUTEX_READ(m_processEntriesLock, lock);
		for (auto& entry : m_processEntries)
			count += u32(entry.canExecuteRemotely && entry.runStatus <= RunStatus_QueuedForRun);
		lock.Leave();
		if (count < m_maxLocalProcessors)
			m_session.DisableRemoteExecution();
		else
			m_session.SetMaxRemoteProcessCount(count);
	}

	void Scheduler::ProcessExited(ExitProcessInfo* info, const ProcessHandle& handle)
	{
		auto ig = MakeGuard([info]() { delete info; });

		bool isLocal = !handle.IsRemote();

		auto si = info->startInfo;

		if (isLocal && si)
			LocalProcessExit(*si, handle);

		{
			SCOPED_CRITICAL_SECTION(m_wasReturnedLock, lock);
			if (info->wasReturned)
				return;
			info->hasCheckedWasReturned = true;
		}

		if (!si) // Can be a process that was reused but didn't get a new process
		{
			UBA_ASSERT(info->processIndex == ~0u);
			return;
		}

		if (!m_useThreadToExitRemoteProcess || isLocal)
		{
			ExitProcess(*info, *handle.m_process, handle.m_process->GetExitCode(), false);
			return;
		}

		// If process is remote we want to return as soon as possible to return worker thread
		ig.Cancel();
		m_finishedRemoteProcessThreads.emplace_back([this, handle = handle, info]()
			{
				ExitProcess(*info, *handle.m_process, handle.m_process->GetExitCode(), false);
				delete info;
				return 0;

			}, TC("UbaScheRemFin"));
	}

	u32 Scheduler::PopCache()
	{
		bool allFinished = true;
		u32 entryToProcess = ~0u;
		auto processEntries = m_processEntries.data();

		for (u32 i=m_processEntriesCacheStart, e=u32(m_processEntries.size()); i!=e; ++i)
		{
			auto& entry = processEntries[i];
			auto cacheStatus = entry.cacheStatus;

			if (cacheStatus >= CacheStatus_Downloading)
			{
				if (allFinished)
					m_processEntriesCacheStart = i;
				continue;
			}

			allFinished = false;

			if (cacheStatus == CacheStatus_QueuedForTest)
			{
				bool canRun = true;
				for (u32 j=0, je=entry.dependencyCount; j!=je; ++j)
				{
					auto depIndex = entry.dependencies[j];
					if (depIndex >= m_processEntries.size())
					{
						m_session.GetLogger().Error(TC("Found dependency on index %u but there are only %u processes registered"), depIndex, u32(m_processEntries.size()));
						return ~0u;
					}
					auto depStatus = processEntries[depIndex].runStatus;
					if (depStatus == RunStatus_Failed || depStatus == RunStatus_Skipped)
					{
						entry.cacheStatus = CacheStatus_Failed;
						entry.runStatus = RunStatus_Skipped;
						return i;
					}
					if (depStatus != RunStatus_Success)
					{
						canRun = false;
						break;
					}
				}
				if (!canRun)
					continue;

				return i;
			}

			if (cacheStatus == CacheStatus_Testing)
				continue;

			UBA_ASSERT(cacheStatus == CacheStatus_QueuedForDownload);
			if (entryToProcess == ~0u)
				entryToProcess = i;
		}
		return entryToProcess;
	}

	u32 Scheduler::PopProcess(bool isLocal, RunStatus& outPrevStatus)
	{
		bool atMaxLocalWeight = m_activeLocalProcessWeight >= float(m_maxLocalProcessors);
		auto processEntries = m_processEntries.data();
		bool allFinished = true;

		for (u32 i=m_processEntriesRunStart, e=u32(m_processEntries.size()); i!=e; ++i)
		{
			auto& entry = processEntries[i];
			auto runStatus = entry.runStatus;

			// Check if we can move search starting point forward
			if (runStatus != RunStatus_QueuedForRun)
			{
				if (allFinished)
				{
					if (runStatus != RunStatus_Running)
						m_processEntriesRunStart = i;
					else
						allFinished = false;
				}
				continue;
			}
			allFinished = false;

			auto cacheStatus = entry.cacheStatus;
			
			if (cacheStatus != CacheStatus_Failed)
				continue;

			UBA_ASSERT(cacheStatus != CacheStatus_Success);

			if (isLocal)
			{
				if (m_forceRemote && entry.canExecuteRemotely)
					continue;
				if (runStatus == RunStatus_QueuedForRun && atMaxLocalWeight)
					continue;
			}
			else
			{
				if (!entry.canExecuteRemotely)
					continue;
			}

			bool canRun = true;
			for (u32 j=0, je=entry.dependencyCount; j!=je; ++j)
			{
				auto depIndex = entry.dependencies[j];
				if (depIndex >= m_processEntries.size())
				{
					m_session.GetLogger().Error(TC("Found dependency on index %u but there are only %u processes registered"), depIndex, u32(m_processEntries.size()));
					return ~0u;
				}
				auto depStatus = processEntries[depIndex].runStatus;
				if (depStatus == RunStatus_Failed || depStatus == RunStatus_Skipped)
				{
					entry.runStatus = RunStatus_Skipped;
					return i;
				}
				if (depStatus != RunStatus_Success)
				{
					canRun = false;
					break;
				}
			}

			if (!canRun)
				continue;

			if (isLocal)
			{
				if (!LocalProcessOkToSpawn())
					return ~0u;

				m_activeLocalProcessWeight += entry.info->weight;
			}

			outPrevStatus = entry.runStatus;
			entry.runStatus = RunStatus_Running;
			return i;
		}

		return ~0u;
	}

	bool Scheduler::RunCacheQuery()
	{
		SCOPED_FUTEX(m_processEntriesLock, lock);
		
		if (m_activeCacheQueries >= m_maxParallelCacheQueries)
			return false;

		u32 entryToProcess = PopCache();
		if (entryToProcess == ~0u)
			return false;

		auto& entry = m_processEntries[entryToProcess];
		auto info = entry.info;

		if (entry.cacheStatus == CacheStatus_QueuedForTest)
		{
			entry.cacheStatus = CacheStatus_Testing;
			++m_activeCacheQueries;
			lock.Leave();

			m_cacheWorkManager->AddWork([this, info, entryToProcess](const WorkContext&)
				{
					ProcessStartInfo2& si = *info;

					MessagePriority priority = m_activeCacheDownloads != 0 ? HasPriority : NormalPriority;
					auto cacheInfo = new CacheFetchInfo(si, m_session, si.rootsHandle, priority);

					bool isHit = false;
					for (auto cacheClient : m_cacheClients)
					{
						if (!cacheClient->FetchEntryFromCache(cacheInfo->result, *cacheInfo, si.cacheBucketId) || !cacheInfo->result.hit)
							continue;

						cacheInfo->client = cacheClient;
						info->cacheInfo = cacheInfo;

						if (m_cancelled)
							break;

						isHit = true;

						SCOPED_FUTEX(m_processEntriesLock, lock);
						m_processEntries[entryToProcess].cacheStatus = CacheStatus_QueuedForDownload;
						--m_activeCacheQueries;
						break;
					}

					if (isHit)
					{
						m_updateThreadWakeup.Set();
						return;
					}

					delete cacheInfo;
					HandleCacheMissed(entryToProcess);
					++m_cacheMissCount;
					UpdateStatusHitMissCount();

				}, 1, TC("UbaSchedTest"));
		}
		else if (entry.cacheStatus == CacheStatus_QueuedForDownload)
		{
			entry.cacheStatus = CacheStatus_Downloading;
			++m_activeCacheQueries;
			lock.Leave();

			++m_activeCacheDownloads;

			m_cacheWorkManager->AddWork([this, info, entryToProcess](const WorkContext&)
				{
					auto cacheInfo = info->cacheInfo;

					ProcessStartInfo2& si = *info;

					bool success = cacheInfo->client->FetchFilesFromCache(cacheInfo->result, *cacheInfo);
					--m_activeCacheDownloads;

					if (success)
					{
						auto process = new CachedProcess(si);
						process->logLines.swap(cacheInfo->result.logLines);
						ProcessHandle ph(process);
						ExitProcessInfo exitInfo;
						exitInfo.scheduler = this;
						exitInfo.startInfo = info;
						exitInfo.isLocal = true;
						exitInfo.processIndex = entryToProcess;
						ExitProcess(exitInfo, *process, 0, true);
						++m_cacheHitCount;
					}
					else
					{
						HandleCacheMissed(entryToProcess);
						++m_cacheMissCount;
					}

					delete cacheInfo;
					UpdateStatusHitMissCount();

				}, 1, TC("UbaSchedDlwd"));
		}
		else
		{
			UBA_ASSERT(entry.cacheStatus == CacheStatus_Failed);
			UBA_ASSERT(entry.runStatus == RunStatus_Skipped);
			UBA_ASSERT(!info->cacheInfo);
			lock.Leave();
			SkipProcess(*info);
		}

		return true;
	}

	bool Scheduler::RunQueuedProcess(bool isLocal)
	{
		while (true)
		{
			RunStatus prevStatus;
			SCOPED_FUTEX(m_processEntriesLock, lock);
			u32 indexToRun = PopProcess(isLocal, prevStatus);
			if (indexToRun == ~0u)
				return false;

			auto& processEntry = m_processEntries[indexToRun];
			auto info = processEntry.info;
			bool canDetour = processEntry.canDetour && !m_forceNative;
			bool wasSkipped = processEntry.runStatus == RunStatus_Skipped;
			UBA_ASSERT(processEntry.cacheStatus == CacheStatus_Failed);
			lock.Leave();

			UpdateQueueCounter(-1);

			if (wasSkipped)
			{
				SkipProcess(*info);
				continue;
			}

			if (isLocal)
				LocalProcessStart(*info);

			UpdateActiveProcessCounter(isLocal, 1);
	
			auto exitInfo = new ExitProcessInfo();
			exitInfo->scheduler = this;
			exitInfo->startInfo = info;
			exitInfo->isLocal = isLocal;
			exitInfo->processIndex = indexToRun;

			ProcessStartInfo si = *info;
			si.userData = exitInfo;
			//si.trackInputs = m_writeToCache && info->cacheBucketId && !m_cacheClients.empty() && info->rules->IsCacheable();
			si.exitedFunc = [](void* userData, const ProcessHandle& handle, ProcessExitedResponse& response)
				{
					auto ei = (ExitProcessInfo*)userData;
					ei->scheduler->ProcessExited(ei, handle);
				};
			UBA_ASSERT(si.rules);

			if (isLocal)
				m_session.RunProcess(si, true, canDetour);
			else
				m_session.RunProcessRemote(si, info->weight, info->knownInputs, info->knownInputsCount);
			return true;
		}
	}

	bool Scheduler::HandleReuseMessage(Process& process, NextProcessInfo& outNextProcess, u32 prevExitCode)
	{
		if (!m_enableProcessReuse)
			return false;

		auto& currentStartInfo = process.GetStartInfo();
		auto ei = (ExitProcessInfo*)currentStartInfo.userData;
		if (!ei) // If null, process has already exited from some other thread
			return false;

		ExitProcess(*ei, process, prevExitCode, false);

		ei->startInfo = nullptr;
		ei->processIndex = ~0u;

		{
			SCOPED_CRITICAL_SECTION(m_wasReturnedLock, lock);
			if (ei->wasReturned)
				return false;
			ei->hasCheckedWasReturned = true;
		}

		bool isLocal = !process.IsRemote();

		while (true)
		{
			RunStatus prevStatus;
			SCOPED_FUTEX(m_processEntriesLock, lock);
			u32 indexToRun = PopProcess(isLocal, prevStatus);
			if (indexToRun == ~0u)
				return false;
			//UBA_ASSERT(prevStatus != RunStatus_QueuedForCache);
			auto& processEntry = m_processEntries[indexToRun];
			auto newInfo = processEntry.info;
			bool wasSkipped = processEntry.runStatus == RunStatus_Skipped;
			lock.Leave();

			UpdateQueueCounter(-1);

			if (wasSkipped)
			{
				SkipProcess(*newInfo);
				continue;
			}

			UpdateActiveProcessCounter(isLocal, 1);

			ei->startInfo = newInfo;
			ei->processIndex = indexToRun;

			auto& si = *newInfo;
			outNextProcess.arguments = si.arguments;
			outNextProcess.workingDir = si.workingDir;
			outNextProcess.description = si.description;
			outNextProcess.logFile = si.logFile;
			outNextProcess.breadcrumbs = si.breadcrumbs;

			#if UBA_DEBUG
			auto PrepPath = [this](StringBufferBase& out, const ProcessStartInfo& psi)
				{
					if (IsAbsolutePath(psi.application))
						FixPath(psi.application, nullptr, 0, out);
					else
						SearchPathForFile(m_session.GetLogger(), out, psi.application, ToView(psi.workingDir), {});
				};
			StringBuffer<> temp1;
			StringBuffer<> temp2;
			PrepPath(temp1, currentStartInfo);
			PrepPath(temp2, si);
			UBA_ASSERTF(temp1.Equals(temp2.data), TC("%s vs %s"), temp1.data, temp2.data);
			#endif
			
			return true;
		}
	}

	void Scheduler::ExitProcess(ExitProcessInfo& info, Process& process, u32 exitCode, bool fromCache)
	{
		auto si = info.startInfo;
		if (!si)
			return;

		ProcessHandle ph;
		ph.m_process = &process;

		ProcessExitedResponse exitedResponse = ProcessExitedResponse_None;
		if (auto func = si->exitedFunc)
			func(si->userData, ph, exitedResponse);

		bool isDone = exitedResponse == ProcessExitedResponse_None;

		SCOPED_FUTEX(m_processEntriesLock, lock);
		auto& entry = m_processEntries[info.processIndex];

		u32* dependencies = entry.dependencies;

		if (isDone)
		{
			entry.runStatus = exitCode == 0 ? RunStatus_Success : RunStatus_Failed;
			entry.info = nullptr;
			entry.dependencies = nullptr;
		}
		else
		{
			UBA_ASSERT(!fromCache);
			UBA_ASSERT(entry.cacheStatus == CacheStatus_Failed);

			entry.canExecuteRemotely = false;
			entry.canDetour = exitedResponse != ProcessExitedResponse_RerunNative;
			entry.runStatus = RunStatus_QueuedForRun;
			m_processEntriesRunStart = Min(m_processEntriesRunStart, info.processIndex);
		}

		if (info.isLocal)
		{
			if (fromCache)
			{
				UBA_ASSERT(isDone);
				entry.cacheStatus = CacheStatus_Success;
				--m_activeCacheQueries;
			}
			else
				m_activeLocalProcessWeight -= si->weight;
		}

		lock.Leave();

		UpdateActiveProcessCounter(info.isLocal, -1);
		m_updateThreadWakeup.Set();

		if (isDone)
		{
			if (exitCode != 0)
				++m_errorCount;

			FinishProcess(ph);
			delete[] dependencies;
			delete si;
		}

		if (m_writeToCache && exitCode == 0)
		{
			// TODO: Read dep.json file
			UBA_ASSERTF(false, TC("Not implemented"));
		}

		ph.m_process = nullptr;
	}

	void Scheduler::SkipProcess(ProcessStartInfo2& info)
	{
		ProcessHandle ph(new SkippedProcess(info));
		ProcessExitedResponse exitedResponse = ProcessExitedResponse_None;
		if (auto func = info.exitedFunc)
			func(info.userData, ph, exitedResponse);
		UBA_ASSERT(exitedResponse == ProcessExitedResponse_None);
		FinishProcess(ph);
		m_updateThreadWakeup.Set();
	}

	void Scheduler::UpdateStatusHitMissCount()
	{
		StringBuffer<> str;
		str.Appendf(TC("Hits %u Misses %u"), m_cacheHitCount.load(), m_cacheMissCount.load());
		m_session.GetTrace().StatusUpdate(1, 6, str, LogEntryType_Info);
	}

	void Scheduler::UpdateQueueCounter(int offset)
	{
		m_queuedProcesses += u32(offset);
		m_session.UpdateProgress(m_totalProcesses, m_finishedProcesses, m_errorCount);
	}

	void Scheduler::UpdateActiveProcessCounter(bool isLocal, int offset)
	{
		if (isLocal)
			m_activeLocalProcesses += u32(offset);
		else
			m_activeRemoteProcesses += u32(offset);
	}

	void Scheduler::FinishProcess(const ProcessHandle& handle)
	{
		if (m_processFinished)
			m_processFinished(handle);
		++m_finishedProcesses;
		m_session.UpdateProgress(m_totalProcesses, m_finishedProcesses, m_errorCount);
	}


	void Scheduler::KillNewestLocalProcess(StringView reason)
	{
		ProcessHandle ph = m_session.GetNewestLocalProcess();
		if (!ph.IsValid())
			return;

		m_session.GetTrace().SchedulerKillProcess(ph.GetId());

		u64 startTime = GetTime();
		if (ProcessReturned(*ph.m_process, true))
			m_session.GetLogger().Info(TC("Killed process %s (%s) - %s"), ph.GetStartInfo().GetDescription(), TimeToText(GetTime() - startTime).str, reason.data);
	}

	void Scheduler::NativeProcessCreated(ProcessImpl& process)
	{
		if (!m_memWatchdog)
			return;

		auto& impl = (ProcessImpl&)process;
		auto ei = (ExitProcessInfo*)impl.m_startInfo.userData;
		
		// Child process, we still need to track
		if (!ei)
		{
			impl.m_startInfo.userData = this;
			impl.m_startInfo.exitedFunc = [](void* userData, const ProcessHandle& handle, ProcessExitedResponse& response)
				{
					auto& impl = *(ProcessImpl*)handle.m_process;
					auto& scheduler = *(Scheduler*)userData;
					StringKey key(impl.m_nativeProcessId, impl.m_nativeProcessCreationTime);
					SCOPED_FUTEX(scheduler.m_processesLock, l);
					bool res = scheduler.m_processes.erase(key) == 1;
					UBA_ASSERT(res);(void)res;
				};
		}

		StringKey key(impl.m_nativeProcessId, impl.m_nativeProcessCreationTime);
		SCOPED_FUTEX(m_processesLock, l);
		m_processes.try_emplace(key, ProcessRecord{ process, ei });
	}

	void Scheduler::LocalProcessStart(ProcessStartInfo2& info)
	{
		if (!m_memWatchdog || !info.memoryGroupId)
			return;

		SCOPED_FUTEX(m_memGroupLookupLock, statsLock);
		auto insres = m_memGroupLookup.try_emplace(info.memoryGroupId);
		MemGroupStats& stats = insres.first->second;
		if (insres.second)
		{
			auto findIt = m_memGroupDb.find(info.memoryGroupId);
			if (findIt != m_memGroupDb.end())
			{
				stats.baseline = findIt->second.baseline;
				stats.average = findIt->second.average;
				stats.history[0] = stats.average;
				stats.historyCounter = 1;
			}
		}
		m_memEstimatedUsage += stats.average;
		++stats.activeProcessCount;
	}

	bool Scheduler::LocalProcessOkToSpawn()
	{
		if (!m_memWaitThreshold)
			return true;

		if (m_memEstimatedUsage < m_memWaitThreshold)
		{
			if (m_memDelaySpawning)
			{
				m_session.GetTrace().SchedulerUpdate(false);
				m_memDelaySpawning = false;
			}
			return true;
		}

		if (!m_activeLocalProcesses) // Always let one run so it can finish eventually
			return true;

		if (m_memDelaySpawning)
			return false;

		m_memDelaySpawning = true;
		m_session.GetTrace().SchedulerUpdate(true);

#if 0//PLATFORM_WINDOWS
		static bool hasBeenRunOnce;
		if (!hasBeenRunOnce)
		{
			hasBeenRunOnce = true;
			auto& logger = m_session.GetLogger();
			logger.BeginScope();
			logger.Info(TC("NOTE - To mitigate this spawn delay it is recommended to make page file larger until you don't see these messages again (Or reduce number of max parallel processes)"));
			logger.Info(TC("       Set max page file to a large number (like 128gb). It will not use disk space unless you actually start using that amount of committed memory"));
			logger.Info(TC("       Also note, this is \"committed\" memory. Not memory in use. So you necessarily don't need more physical memory"));
			MEMORYSTATUSEX memStatus = { sizeof(memStatus) };
			GlobalMemoryStatusEx(&memStatus);
			logger.Info(TC("  TotalPhys: %s"), BytesToText(memStatus.ullTotalPhys));
			logger.Info(TC("  AvailPhys: %s"), BytesToText(memStatus.ullAvailPhys));
			logger.Info(TC("  TotalPage: %s"), BytesToText(memStatus.ullTotalPageFile));
			logger.Info(TC("  AvailPage: %s"), BytesToText(memStatus.ullAvailPageFile));
			logger.EndScope();
		}
#endif
		return false;
	}

	void Scheduler::LocalProcessExit(ProcessStartInfo2& info, const ProcessHandle& handle)
	{
		if (!m_memWatchdog)
			return;

		auto& impl = *(ProcessImpl*)handle.m_process;

		if (info.memoryGroupId)
		{
			u64 processPeakMemory = impl.m_processStats.peakMemory;

			SCOPED_FUTEX(m_memGroupLookupLock, statsLock);
			MemGroupStats& stats = m_memGroupLookup[info.memoryGroupId];

			if (!stats.historyCounter) // First process in memory group could be pch
			{
				for (auto& kv : impl.m_shared.writtenFiles)
				{
					auto& writtenFile = kv.second;
					if (StringView(writtenFile.name).EndsWith(TC(".pch")))
					{
						stats.baseline = writtenFile.mappingWritten;
						processPeakMemory = stats.baseline;
					}
				}
			}			

			--stats.activeProcessCount;

			if (processPeakMemory >= stats.baseline) // Don't know if this test is too aggressive.. but 
			{
				u64& peakMemory = stats.history[stats.historyCounter % sizeof_array(stats.history)];
				peakMemory = processPeakMemory;
				++stats.historyCounter;

				u32 count = Min(u32(sizeof_array(stats.history)), stats.historyCounter);
				u64 average = 0;
				for (u32 i=0;i!=count;++i)
					average += stats.history[i];
				average /= count;

				if (average < stats.baseline)
					average = stats.baseline;
				stats.average = average;
			}

			m_memEstimatedUsage -= stats.average;
		}

		if (impl.m_nativeProcessCreationTime)
		{
			StringKey key(impl.m_nativeProcessId, impl.m_nativeProcessCreationTime);
			SCOPED_FUTEX(m_processesLock, l);
			bool res = m_processes.erase(key) == 1;
			UBA_ASSERT(res);(void)res;
		}
	}

	void Scheduler::LoadMemTrackTable()
	{
		StringBuffer<> fileName;
		fileName.Append(m_session.GetRootDir()).Append("memgroups");
		FileAccessor file(m_session.GetLogger(), fileName.data);
		if (!file.OpenMemoryRead(0, false) || !file.GetSize())
			return;
		BinaryReader reader(file.GetData(), 0, file.GetSize());
		
		u16 version = reader.ReadU16();
		if (version != 1)
			return;

		while (reader.GetLeft() >= 20)
		{
			u32 key = reader.ReadU32();
			u64 baseline = reader.ReadU64();
			u64 average = reader.ReadU64();
			m_memGroupDb[key] = MemGroupEntry{ baseline, average };
		}
	}

	void Scheduler::SaveMemTrackTable()
	{
		for (auto& kv : m_memGroupLookup)
			m_memGroupDb[kv.first] = MemGroupEntry{ kv.second.baseline, kv.second.average };

		StringBuffer<> fileName;
		fileName.Append(m_session.GetRootDir()).Append("memgroups");
		FileAccessor file(m_session.GetLogger(), fileName.data);

		u64 fileSize = 2 + m_memGroupDb.size()*20;

		if (!file.CreateMemoryWrite(false, DefaultAttributes(), fileSize))
			return;

		BinaryWriter writer(file.GetData(), 0, fileSize);

		writer.WriteU16(1); // Version

		for (auto& kv : m_memGroupDb)
		{
			writer.WriteU32(kv.first);
			writer.WriteU64(kv.second.baseline);
			writer.WriteU64(kv.second.average);
		}

		file.Close();
	}

	void Scheduler::ThreadMemoryCheckLoop()
	{
		auto& logger = m_session.GetLogger();

		//u64 randomKillTime = GetTime() + MsToTime(5000);

		Vector<u8> queryBuffer(1024 * 1024);

		#if PLATFORM_WINDOWS
		struct UBA_SYSTEM_PERFORMANCE_INFORMATION
		{
			LARGE_INTEGER IdleTime;
			LARGE_INTEGER ReadTransferCount;
			LARGE_INTEGER WriteTransferCount;
			LARGE_INTEGER OtherTransferCount;
			ULONG ReadOperationCount;
			ULONG WriteOperationCount;
			ULONG OtherOperationCount;
			ULONG AvailablePages;
			ULONG TotalCommittedPages;
			ULONG TotalCommitLimit;
			ULONG PeakCommitment;
			ULONG PageFaults;
			ULONG WriteCopyFaults;
			ULONG TransitionFaults;
			ULONG CacheTransitionFaults;
			ULONG DemandZeroFaults;
			ULONG PagesRead;
			ULONG PageReadIos;
			ULONG CacheReads;
			ULONG CacheIos;
			ULONG PagefilePagesWritten;
			// ...
		};
		static_assert(sizeof(UBA_SYSTEM_PERFORMANCE_INFORMATION) <= sizeof(SYSTEM_PERFORMANCE_INFORMATION));
		UBA_SYSTEM_PERFORMANCE_INFORMATION prevPerfInfo = {};
		bool firstPerfInfo = true;
		#endif

		constexpr u32 StartRow = 20;

		Trace& trace = m_session.GetTrace();

		if (m_memTracingLevel >= 1)
		{
			trace.StatusUpdate(StartRow, 1, TCV("MemTot"), LogEntryType_Info);
			trace.StatusUpdate(StartRow+1, 1, TCV("MemTot Est"), LogEntryType_Info);
			trace.StatusUpdate(StartRow, 10, TCV("MemProc"), LogEntryType_Info);
			trace.StatusUpdate(StartRow+1, 10, TCV("MemProc Est"), LogEntryType_Info);
			trace.StatusUpdate(StartRow+2, 1, TCV("MemBaseline"), LogEntryType_Info);
			trace.StatusUpdate(StartRow+3, 1, TCV("MemUntrack"), LogEntryType_Info);
			trace.StatusUpdate(StartRow+4, 1, TCV("MemStartWait"), LogEntryType_Info);
			trace.StatusUpdate(StartRow+5, 1, TCV("MemStartKill"), LogEntryType_Info);
			trace.StatusUpdate(StartRow+6, 1, TCV("PagefileRead"), LogEntryType_Info);
			trace.StatusUpdate(StartRow+7, 1, TCV("PagefileWrite"), LogEntryType_Info);
		}

		u64 memAvail;
		u64 memTotal;
		u64 maxPageFile;
		if (!GetMemoryInfo(logger, memAvail, memTotal, &maxPageFile))
		{
			m_memWatchdog = false;
			logger.Warning(TC("GetMemoryInfo failed. Memory tracking disabled."));
			m_updateThreadWakeup.Set();
			return;
		}
		u64 memPhys = memTotal - maxPageFile;

		UnorderedMap<u32, u32> memoryGroupToIndex;

		u64 memKillThreshold = 0;
		

		u8 memStartWaitPercent = m_memStartWaitPercent;
		u8 memStartWaitPercentMin = u8(double(memPhys)/double(memTotal) * 100.0f);
		u64 lastPageFileWriteTimeMs = 0;

		u32 timeoutMs = 0;

		StringBuffer<> str;

		while (!m_memThreadEvent.IsSet(timeoutMs))
		{
			timeoutMs = 1000;

			GetMemoryInfo(logger, memAvail, memTotal, &maxPageFile);

			m_memWaitThreshold = u64(double(memTotal) * double(memStartWaitPercent) / 100.0);
			memKillThreshold = u64(double(memTotal) * double(m_memStartKillPercent) / 100.0);

			u64 untrackedMemory = 0;
			u64 processMemTotalPageCount = 0;

			#if PLATFORM_WINDOWS
			ULONG returnedSize;
			while (true)
			{
				NTSTATUS res = NtQuerySystemInformation(SystemProcessInformation, queryBuffer.data(), (u32)queryBuffer.size(), &returnedSize);
				if (res == STATUS_SUCCESS)
					break;
				if (res == STATUS_INFO_LENGTH_MISMATCH)
				{
					queryBuffer.resize(returnedSize + (10*PagefileUsageOffset));
					continue;
				}

				m_memWatchdog = false;
				logger.Warning(TC("NtQuerySystemInformation failed. Memory tracking disabled."));
				m_updateThreadWakeup.Set();
				break;
			}

			u8* it = queryBuffer.data();
			ULONG nextEntryOffset = *(ULONG*)it;
			if (nextEntryOffset < PagefileUsageOffset + 8)
			{
				m_memWatchdog = false;
				logger.Warning(TC("NtQuerySystemInformation does not contain PageFileUsage. Memory tracking disabled."));
				m_updateThreadWakeup.Set();
				break;
			}

			u32 activeProcessCount;
			{
				SCOPED_FUTEX(m_processesLock, l);
				activeProcessCount = u32(m_processes.size());

				while (nextEntryOffset)
				{
					u64 createTime = *(u64*)(it + CreateTimeOffset);
					u32 pid = *(u32*)(it + PidOffset);
					auto procIt = m_processes.find(StringKey(pid, createTime));
					if (procIt != m_processes.end())
					{
						ProcessRecord rec = procIt->second;
						u64 procMem = *(u64*)(it + PagefileUsageOffset);
						if (rec.ep)
							if (rec.ep->startInfo->memoryGroupId == 0)
								untrackedMemory += procMem;
						AtomicMax(rec.process.m_processStats.peakMemory, procMem);
						processMemTotalPageCount += procMem;
					}

					nextEntryOffset = *(ULONG*)it;
					it += nextEntryOffset;
				}
			}
			#endif

			u64 memUsed = memTotal - memAvail;
			u64 memBaseLine = memUsed - processMemTotalPageCount;
			u64 estimatedProcessMemoryUsage = 0;
			u64 estimatedMemoryUsage = 0;

			{
				SCOPED_FUTEX(m_memGroupLookupLock, statsLock);

				for (auto& kv : m_memGroupLookup)
				{
					MemGroupStats& stats = kv.second;
					estimatedProcessMemoryUsage += stats.activeProcessCount * stats.average;

					if (m_memTracingLevel >= 2)
					{
						constexpr u32 colIndex = 25;
						auto res = memoryGroupToIndex.emplace(kv.first, 0u);
						u32& index = res.first->second;
						if (res.second)
						{
							index = StartRow + u32(memoryGroupToIndex.size() - 1);
							str.Clear().AppendValue(kv.first);
							trace.StatusUpdate(index, colIndex, str, LogEntryType_Info);
						}

						str.Clear().Append(BytesToText(stats.average).str);
						trace.StatusUpdate(index, colIndex + 5, str, LogEntryType_Info);
					}
				}
				estimatedMemoryUsage = memBaseLine + untrackedMemory + estimatedProcessMemoryUsage;
				m_memEstimatedUsage = estimatedMemoryUsage < memUsed ? memUsed : estimatedMemoryUsage;
			}

			if (m_memTracingLevel >= 1)
			{
				trace.StatusUpdate(StartRow, 6, BytesToText(memUsed), LogEntryType_Info);
				trace.StatusUpdate(StartRow+1, 6, BytesToText(estimatedMemoryUsage), LogEntryType_Info);
				trace.StatusUpdate(StartRow, 15, BytesToText(processMemTotalPageCount), LogEntryType_Info);
				trace.StatusUpdate(StartRow+1, 15, BytesToText(estimatedProcessMemoryUsage), LogEntryType_Info);
				trace.StatusUpdate(StartRow+2, 6, BytesToText(memBaseLine), LogEntryType_Info);
				trace.StatusUpdate(StartRow+3, 6, BytesToText(untrackedMemory), LogEntryType_Info);
				trace.StatusUpdate(StartRow+4, 6, BytesToText(m_memWaitThreshold), LogEntryType_Info);
				trace.StatusUpdate(StartRow+5, 6, BytesToText(memKillThreshold), LogEntryType_Info);
			}

			if (memKillThreshold)
			{
				while (memUsed > memKillThreshold)
				{
					str.Clear().Appendf(TC("Low on memory (%s/%s). Kill threshold is %s"), BytesToText(memUsed).str, BytesToText(memTotal).str, BytesToText(memKillThreshold).str);
					KillNewestLocalProcess(str);
					if (!GetMemoryInfo(logger, memAvail, memTotal, &maxPageFile))
						break;
					memUsed = memTotal - memAvail;
				}
			}

			#if PLATFORM_WINDOWS
			if (m_memPagefileActivityThreshold)
			{
				auto& currPerfInfo = *(UBA_SYSTEM_PERFORMANCE_INFORMATION*)queryBuffer.data();
				if (NtQuerySystemInformation(SystemPerformanceInformation, &currPerfInfo, sizeof(SYSTEM_PERFORMANCE_INFORMATION), NULL) == STATUS_SUCCESS && !firstPerfInfo)
				{
					ULONG reads = currPerfInfo.PagesRead - prevPerfInfo.PagesRead;
					ULONG writes = currPerfInfo.PagefilePagesWritten - prevPerfInfo.PagefilePagesWritten;

					if (m_memTracingLevel >= 1)
					{
						trace.StatusUpdate(StartRow+6, 6, CountToText(reads), LogEntryType_Info);
						trace.StatusUpdate(StartRow+7, 6, CountToText(writes), LogEntryType_Info);
					}

					if (memUsed > memPhys && activeProcessCount > 6 && writes > m_memPagefileActivityThreshold)
					{
						memStartWaitPercent = u8(double(memUsed)/double(memTotal) * 100.0f);
						str.Clear().Appendf(TC("Pagefile activity high (%lu writes). MemUsed %s, MemPhys %s, MemTot %s"), writes, BytesToText(memUsed).str, BytesToText(memPhys).str, BytesToText(memTotal).str);
						KillNewestLocalProcess(str);
						lastPageFileWriteTimeMs = TimeToMs(GetTime());
					}
					else if (writes > 0)
					{
						if (memStartWaitPercent > memStartWaitPercentMin)
							--memStartWaitPercent;
						lastPageFileWriteTimeMs = TimeToMs(GetTime());
					}
					else if (TimeToMs(GetTime()) - lastPageFileWriteTimeMs > 5*1000)
					{
						if (memStartWaitPercent < m_memStartWaitPercent)
						{
							++memStartWaitPercent;
							if (memStartWaitPercent == m_memStartWaitPercent)
								m_updateThreadWakeup.Set(); // Just in case.. this should never happen in practice
						}
					}
				}
				firstPerfInfo = false;
				prevPerfInfo = currPerfInfo;
			}
			#endif

			//if (randomKillTime < GetTime())
			//{
			//	randomKillTime = GetTime() + MsToTime(500);
			//	if (m_activeLocalProcesses > 4)
			//		KillNewestLocalProcess();
			//}
		}
	}

	bool Scheduler::EnqueueFromFile(const tchar* yamlFilename, const Function<void(EnqueueProcessInfo&)>& enqueued)
	{
		auto& logger = m_session.GetLogger();

		TString app;
		TString arg;
		TString dir;
		TString desc;
		bool allowDetour = true;
		bool allowRemote = true;
		float weight = 1.0f;
		u32 cacheBucket = 0;
		u32 memoryGroup = 0;
		Vector<u32> deps;

		ProcessStartInfo si;

		auto enqueueProcess = [&]()
			{
				si.application = app.c_str();
				si.arguments = arg.c_str();
				si.workingDir = dir.c_str();
				si.description = desc.c_str();

				#if UBA_DEBUG
				StringBuffer<> logFile;
				if (true)
				{
					static u32 processId = 1; // TODO: This should be done in a better way.. or not at all?
					GenerateNameForProcess(logFile, si.arguments, ++processId);
					logFile.Append(TCV(".log"));
					si.logFile = logFile.data;
				};
				#endif

				EnqueueProcessInfo info { si };
				info.dependencies = deps.data();
				info.dependencyCount = u32(deps.size());
				info.canDetour = allowDetour;
				info.canExecuteRemotely = allowRemote;
				info.weight = weight;
				info.cacheBucketId = cacheBucket;
				info.memoryGroupId = memoryGroup;
				if (enqueued)
					enqueued(info);
				EnqueueProcess(info);
				app.clear();
				arg.clear();
				dir.clear();
				desc.clear();
				deps.clear();
				allowDetour = true;
				allowRemote = true;
				weight = 1.0f;
				cacheBucket = 0;
				memoryGroup = 0;
			};

		enum InsideArray
		{
			InsideArray_None,
			InsideArray_CacheRoots,
			InsideArray_Processes,
		};

		InsideArray insideArray = InsideArray_None;

		auto readLine = [&](const TString& line)
			{
				const tchar* keyStart = line.c_str();
				while (*keyStart && *keyStart == ' ')
					++keyStart;
				if (!*keyStart)
					return true;
				u32 indentation = u32(keyStart - line.c_str());

				if (insideArray != InsideArray_None && !indentation)
					insideArray = InsideArray_None;

				StringBuffer<32> key;
				const tchar* valueStart = nullptr;

				if (*keyStart == '-')
				{
					UBA_ASSERT(insideArray != InsideArray_None);
					valueStart = keyStart + 2;
				}
				else
				{
					const tchar* colon = TStrchr(keyStart, ':');
					if (!colon)
						return false;
					key.Append(keyStart, colon - keyStart);
					valueStart = colon + 1;
					while (*valueStart && *valueStart == ' ')
						++valueStart;
				}

				switch (insideArray)
				{
				case InsideArray_None:
				{
					if (key.Equals(TCV("environment")))
					{
						#if PLATFORM_WINDOWS
						SetEnvironmentVariable(TC("PATH"), valueStart);
						#endif
						return true;
					}
					if (key.Equals(TCV("cacheroots")))
					{
						insideArray = InsideArray_CacheRoots;
						return true;
					}
					if (key.Equals(TCV("processes")))
					{
						insideArray = InsideArray_Processes;
						return true;
					}
					return true;
				}
				case InsideArray_CacheRoots:
				{
					auto& rootPaths = *m_rootPaths.emplace_back(new RootPaths());
					if (Equals(valueStart, TC("SystemRoots")))
						rootPaths.RegisterSystemRoots(logger);
					else
						rootPaths.RegisterRoot(logger, valueStart);
					return true;
				}
				case InsideArray_Processes:
				{
					if (*keyStart == '-')
					{
						keyStart += 2;
						if (!app.empty())
							enqueueProcess();
					}

					if (key.Equals(TCV("app")))
						app = valueStart;
					else if (key.Equals(TCV("arg")))
						arg = valueStart;
					else if (key.Equals(TCV("dir")))
						dir = valueStart;
					else if (key.Equals(TCV("desc")))
						desc = valueStart;
					else if (key.Equals(TCV("detour")))
						allowDetour = !Equals(valueStart, TC("false"));
					else if (key.Equals(TCV("remote")))
						allowRemote = !Equals(valueStart, TC("false"));
					else if (key.Equals(TCV("weight")))
						StringBuffer<32>(valueStart).Parse(weight);
					else if (key.Equals(TCV("cache")))
						StringBuffer<32>(valueStart).Parse(cacheBucket);
					else if (key.Equals(TCV("memgroup")))
						StringBuffer<32>(valueStart).Parse(memoryGroup);
					else if (key.Equals(TCV("dep")))
					{
						const tchar* depStart = TStrchr(valueStart, '[');
						if (!depStart)
							return false;
						++depStart;
						StringBuffer<32> depStr;
						for (const tchar* it = depStart; *it; ++it)
						{
							if (*it != ']' && *it != ',')
							{
								if (*it != ' ')
									depStr.Append(*it);
								continue;
							}
							u32 depIndex;
							if (!depStr.Parse(depIndex))
								return false;
							depStr.Clear();
							deps.push_back(depIndex);

							if (!*it)
								break;
							depStart = it + 1;
						}
					}
					return true;
				}
				}
				return true;
			};

		if (!ReadLines(logger, yamlFilename, readLine))
			return false;

		if (!app.empty())
			enqueueProcess();

		return true;
	}

	bool Scheduler::EnqueueFromSpecialJson(const tchar* jsonFilename, const tchar* workingDir, const tchar* description, RootsHandle rootsHandle, void* userData)
	{
		Logger& logger = m_session.GetLogger();
		FileAccessor fa(logger, jsonFilename);
		if (!fa.OpenMemoryRead())
			return false;

		auto data = (const char*)fa.GetData();
		u64 dataLen = fa.GetSize();
		auto i = data;
		auto e = data + dataLen;
		u32 scope = 0;
		const char* stringStart = nullptr;
		std::string lastString;
		char lastChar = 0;

		struct Command { TString application; TString arguments; };
		Vector<Command> commands;

		while (i != e)
		{
			if (!stringStart)
			{
				if (*i == '{')
				{
					++scope;
				}
				else if (*i == '}')
				{
					--scope;
				}
				else if (*i == '\"' && lastChar != '\\')
				{
					stringStart = i+1;
				}
			}
			else
			{
				if (*i == '\"' && lastChar != '\\')
				{
					if (lastString == "command")
					{
						Command& command = commands.emplace_back();
						StringBuffer<2048> args;
						ParseArguments(stringStart, int(i - stringStart), [&](char* arg, u32 argLen)
						{
							// Strip out double backslash
							char* readIt = arg;
							char* writeIt = arg;
							char last = 0;
							while (true)
							{
								char c = *readIt;
								*writeIt = c;
								if (!(c == '\\' && last == '\\'))
									++writeIt;
								if (c == 0)
									break;
								++readIt;
								last = c;
							};
							argLen = u32(writeIt - arg);

							if (command.application.empty())
							{
								command.application.assign(arg, arg + argLen);
								return;
							}
							if (args.count)
								args.Append(' ');
							args.Append(arg, argLen);
						});
						command.arguments = args.ToString();
					}
					lastString.assign(stringStart, int(i - stringStart));
					stringStart = nullptr;
				}
			}
			lastChar = *i;
			++i;
		}
		UBA_ASSERT(scope == 0);

		float weight = 0;
		if (userData)
		{
			auto& ei = *(ExitProcessInfo*)userData;
			weight = ei.startInfo->weight;
		}

		// Return weight while running these tasks
		SCOPED_FUTEX(m_processEntriesLock, lock);
		m_activeLocalProcessWeight -= weight;
		lock.Leave();

		Event done(true);
		struct Context
		{
			Logger& logger;
			Event& done;
			Atomic<u32> counter;
		} context { logger, done };

		auto exitedFunc = [](void* userData, const ProcessHandle& ph, ProcessExitedResponse&)
			{
				auto& context = *(Context*)userData;
				if (ph.GetExitCode() != 0 && ph.GetExecutionType() != ProcessExecutionType_Skipped)
					for (auto& line : ph.GetLogLines())
						context.logger.Log(LogEntryType_Error, line.text);

				if (!--context.counter)
					context.done.Set();
			};

		for (auto& command : commands)
		{
			StringBuffer<> application(command.application);
			m_session.DevirtualizePath(application, rootsHandle);
			//StringBuffer<> logFile;
			//logFile.Appendf(L"%s_LOG_FILE_%u.log", description, context.counter.load());
			++context.counter;
			ProcessStartInfo si;
			si.application = application.data;
			si.workingDir = workingDir;
			si.arguments = command.arguments.c_str();
			si.description = description;
			si.exitedFunc = exitedFunc;
			si.userData = &context;
			si.rootsHandle = rootsHandle;
			//si.logFile = logFile.data;
			EnqueueProcess({si});
		}

		m_session.ReenableRemoteExecution();

		if (!done.IsSet(2*60*60*1000))
			logger.Error(TC("Something went wrong waiting for %s"), description);

		// Take back weight.. TODO: Should this wait for available weight before returning?
		lock.Enter();
		m_activeLocalProcessWeight += weight;
		lock.Leave();

		return true;
	}

	////////////////////////////////////////////////////////////////////////////////////////////////////
}

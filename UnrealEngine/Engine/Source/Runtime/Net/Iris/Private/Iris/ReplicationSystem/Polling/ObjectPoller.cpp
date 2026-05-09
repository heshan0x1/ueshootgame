// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================================================================
// 文件作用：FObjectPoller 实现 + FReplicationPollTask 并行任务定义。
//
// 并行调度算法（关键）：
//   1) 把整个 ObjectsConsideredForPolling 位图按 "Chunk = NumWordsPerChunk * 32 个对象" 切片；
//   2) 创建 NumPollingTasks（默认 32）个 task；
//   3) 第 i 个 task 从 bit (i*BitsPerChunk) 开始，每处理完一个 chunk 跳 ChunkStride = N*BitsPerChunk 个 bit；
//      → 各 task 拿到的是"间隔交错"的若干 chunk，避免连续 0 段（无对象的稀疏区域）造成负载倾斜；
//   4) UE::Tasks::Wait 收尾，allowed-to-run-on-current-thread 让 GameThread 也参与 task 出栈。
//
// 触发条件：必须 (a) AreParallelTasksAllowed() 返回 true (b) 对象数足够 (BitsPerChunk*NumPollingTasks < total bits)。
// 否则回退到同步路径（直接 ForAllSetBits）。
//
// CVars：
//   - net.Iris.Poll.FilterOutNonDirtyPushBasedObjects (true)：在并行前先做位图运算，把"全 push 且未脏 GC"的对象屏蔽，
//     节省 task 内部的判定成本；
//   - net.Iris.Poll.EnableVerboseProfiling (false)：打开后每个对象的 poll 都会发 profiler scope，观察具体属性；
//   - net.Iris.NumPollingTasks (32)：并行 task 数，0 = 同步；
//   - net.Iris.NumWordsToInterleave (16)：每 chunk 包含的 32-bit words 数（默认 16 words = 64 字节 ≈ 1 cache line）。
// =============================================================================================================================

#include "ObjectPoller.h"

#include "Async/AsyncWork.h"
#include "Async/TaskGraphInterfaces.h"

#include "Iris/ReplicationSystem/ObjectReplicationBridge.h"
#include "Iris/ReplicationSystem/ReplicationOperations.h"
#include "Iris/ReplicationSystem/ReplicationSystem.h"
#include "Iris/ReplicationSystem/ReplicationSystemInternal.h"
#include "Iris/ReplicationState/ReplicationStateStorage.h"
#include "Iris/ReplicationSystem/NetRefHandleManager.h"
#include "Iris/ReplicationSystem/LegacyPushModel.h"
#include "Iris/ReplicationSystem/NetRefHandle.h"

#include "Iris/Core/IrisProfiler.h"
#include "Iris/Core/IrisCsv.h"
#include "Iris/Stats/NetStatsContext.h"

#include "Net/Core/DirtyNetObjectTracker/GlobalDirtyNetObjectTracker.h"
#include "Net/Core/Trace/NetTrace.h"
#include "Net/Core/Trace/NetDebugName.h"
#include "HAL/IConsoleManager.h"

#include "Tasks/Task.h"

namespace UE::Net::Private
{
namespace CVars
{
	// 把"完全 push-based 且本帧未标脏 / 未受 GC 影响"的对象提前从 polling 列表中剔除，可显著降低 task 内的分支判定。
	static bool bFilterOutNonDirtyPushBasedObjects = true;
	static FAutoConsoleVariableRef CVarFilterOutNonDirtyPushBasedObjects(
		TEXT("net.Iris.Poll.FilterOutNonDirtyPushBasedObjects"),
		bFilterOutNonDirtyPushBasedObjects,
		TEXT("When true fully push based objects that are not considered to require polling are masked off before the poll loop.")
	);

	// 调试用：开启后每个属性的 poll 都会有一个 profiler scope（profile 时能定位到具体属性）。
	static bool bEnableVerbosePollProfiling = false;
	static FAutoConsoleVariableRef CVarEnableVerbosePollProfiling(
		TEXT("net.Iris.Poll.EnableVerboseProfiling"),
		bEnableVerbosePollProfiling,
		TEXT("Enable conditional profiler scopes for polling. Useful to see exactly what properties gets polled in cpu profiler..")
	);

	// 0 = 同步轮询；>0 = 并行 task 数。32 适合大多数现代 CPU。
	static int32 GNumPollingTasks = 32;
	static FAutoConsoleVariableRef CvarNumPollingTasks(
		TEXT("net.Iris.NumPollingTasks"),
		GNumPollingTasks,
		TEXT("Number of Tasks that will be created to poll dirty objects in a single frame. Setting to 0 will run Polling Synchronously instead.")
	);

	// 每 chunk 多少个 32-bit word（位图的 word size）。16 → 16*32 = 512 bits / 64 bytes，刚好一条缓存行。
	static int32 GNumWordsPerChunk = 16;
	static FAutoConsoleVariableRef CvarNumWordsToInterleave(
		TEXT("net.Iris.NumWordsToInterleave"),
		GNumWordsPerChunk,
		TEXT("Number of words in a row to interleave before queuing to the next task")
	);
}

	// 单个 polling task 的输入数据。
	struct FReplicationPollTaskData
	{
		const FNetBitArrayView ObjectsConsideredForPolling; // Represents the entire set of objects to poll across all tasks, and this Task will Poll a subset of them
		FNetTypeStats* NetTypeStats = nullptr; // Task will use NetTypeStats to get an available ChildNetStatsContext to use for the duration of this Task
		FObjectPoller::FPreUpdateAndPollStats PollStats; // Standalone PollStats object which will be combined with the main instance when tasks are joined
		uint32 CurrentTaskIndex = 0; // Unique index per task, must be contiguous as it is used to define which set of chunks will be processed by this Task
		uint32 BitsPerChunk = 0; // Number of bits are processed in a single chunk, should be at least 1 cache line to avoid false sharing
		uint32 ChunkStride = 0; // When a Chunk has been processed, we move on by ChunkStride which skips over the bits processed by the other tasks
		bool bUsePushModel = false; // Selects between PushModelPollObject and ForcePollObject

		FReplicationPollTaskData(const FNetBitArrayView& ObjectsConsideredForPollingIn)
		: ObjectsConsideredForPolling(ObjectsConsideredForPollingIn)
		{}
	};

	// 单个 polling task 的运行体。同步路径下也可单独构造调用 DoTask（但目前未走此路径）。
	class FReplicationPollTask
	{
	protected:
		FObjectPoller* Poller = nullptr;
		FReplicationPollTaskData& PollTaskData;
		FReplicationSystemInternal* ReplicationSystemInternal = nullptr;

	public:
		

		FReplicationPollTask(
			FObjectPoller* InPoller,
			FReplicationPollTaskData& InPollTaskData,
			FReplicationSystemInternal* InReplicationSystemInternal
		)
			: Poller(InPoller)
			, PollTaskData(InPollTaskData)
			, ReplicationSystemInternal(InReplicationSystemInternal)
		{
		}

		void DoTask() const
		{
			// 必须在并行阶段——调用方会 StartParallelPhase()/StopParallelPhase() 设置该状态。
			check(ReplicationSystemInternal->GetIsInParallelPhase());

			IRIS_CSV_PROFILER_SCOPE(Iris, ReplicationPollAndCopyTask);

			IRIS_PROFILER_SCOPE_CONDITIONAL(PollAndCopyPushBased, PollTaskData.bUsePushModel);
			IRIS_PROFILER_SCOPE_CONDITIONAL(ForcePollAndCopy, !PollTaskData.bUsePushModel);

			// 每个 task 申请一个独立的子 NetStatsContext，避免多线程争用累积统计；DoTask 末尾归还。
			FNetStatsContext* ChildNetStatsContext = PollTaskData.NetTypeStats->AcquireChildNetStatsContext();

			//Set up the lambda to use for polling, selecting a different one for PushModel or ForcePoll
			// 选择 PushModel 或 ForcePoll 路径——一次决策，避免在每对象上做分支。
			TFunction<void(uint32 ObjectIndex)> PollingLambdaFunc = PollTaskData.bUsePushModel ?
				TFunction<void(uint32 ObjectIndex)>
				{
					[this, ChildNetStatsContext](uint32 ObjectIndex)
					{
						Poller->PushModelPollObject(ObjectIndex, ChildNetStatsContext, PollTaskData.PollStats);
					}
				} :
				TFunction<void(uint32 ObjectIndex)>
				{
					[this, ChildNetStatsContext](uint32 ObjectIndex)
					{
						Poller->ForcePollObject(ObjectIndex, ChildNetStatsContext, PollTaskData.PollStats);
					}
				};

			//   For each Task, we want to take the objects list and process it based on
			//Chunks of a specific size, and interleave these Chunks between tasks
			//   This is more balanced than dividing into contiguous chunks as the object bit array
			//ends up with long sets of 0 values that would result in some very short tasks
			// 起始 bit = TaskIndex * BitsPerChunk；之后每处理完一个 chunk 跳 ChunkStride = N*BitsPerChunk。
			// → 第 0 个 task 处理 chunks {0, N, 2N, ...}；第 1 个 task 处理 {1, N+1, 2N+1, ...}；以此类推。
			// 这样每个 task 都能均匀拿到稀疏 / 密集区域的混合，避免某 task 拿到全 0 区段而早早结束。
			uint32 CurrentBit = PollTaskData.CurrentTaskIndex * PollTaskData.BitsPerChunk;

			while (CurrentBit < PollTaskData.ObjectsConsideredForPolling.GetNumBits())
			{
				const uint32 ObjectIndexRangeStart = CurrentBit;
				uint32 ObjectIndexRangeEnd = CurrentBit + PollTaskData.BitsPerChunk - 1;

				// 截到位图末尾。
				if (ObjectIndexRangeEnd >= PollTaskData.ObjectsConsideredForPolling.GetNumBits())
				{
					ObjectIndexRangeEnd = PollTaskData.ObjectsConsideredForPolling.GetNumBits();
				}

				
				// ForAllSetBitsInRange：仅对该 chunk 内置位的 bit 调用 lambda。
				PollTaskData.ObjectsConsideredForPolling.ForAllSetBitsInRange(ObjectIndexRangeStart, ObjectIndexRangeEnd, PollingLambdaFunc);
				
				//Move the CurrentBit on by ChunkStride so we access the next part of the array which is designated for this Task to process
				CurrentBit += PollTaskData.ChunkStride;
			}

			PollTaskData.NetTypeStats->ReleaseChildNetStatsContext(ChildNetStatsContext);
		}
	};

FObjectPoller::FObjectPoller(const FInitParams& InitParams)
	: ObjectReplicationBridge(InitParams.ObjectReplicationBridge)
	, ReplicationSystemInternal(InitParams.ReplicationSystemInternal)
	, LocalNetRefHandleManager(ReplicationSystemInternal->GetNetRefHandleManager())
	, NetStatsContext(nullptr)
	, ReplicatedInstances(LocalNetRefHandleManager.GetReplicatedInstances())
	, AccumulatedDirtyObjects(ReplicationSystemInternal->GetDirtyNetObjectTracker().GetAccumulatedDirtyNetObjects())
	, DirtyObjectsToQuantize(LocalNetRefHandleManager.GetDirtyObjectsToQuantize())
{
	GarbageCollectionAffectedObjects = MakeNetBitArrayView(ObjectReplicationBridge->GarbageCollectionAffectedObjects);

	// DirtyObjectsThisFrame is acquired only during polling 
	// DirtyObjectsThisFrame 在 PollAndCopyObjects/PollAndCopySingleObject 中通过 FDirtyObjectsAccessor 临时获取（RAII 锁定）。
	bUsePerPropertyDirtyTracking = FGlobalDirtyNetObjectTracker::IsUsingPerPropertyDirtyTracking();
}

void FObjectPoller::PollAndCopyObjects(const FNetBitArrayView& InObjectsConsideredForPolling)
{
	// FDirtyObjectsAccessor：RAII 持有锁定 DirtyObjectsThisFrame 位图，让本帧之内的"新标脏"暂存在另一组缓冲。
	// 析构时自动解锁——这样 poll 期间可以安全地在脏位图上 SetBit 而不与外部并发冲突。
	FDirtyObjectsAccessor DirtyObjectsAccessor(ReplicationSystemInternal->GetDirtyNetObjectTracker());
	DirtyObjectsThisFrame = DirtyObjectsAccessor.GetDirtyNetObjects();

	// GameThread 的 NetStatsContext（同步路径直接用；并行路径下每 task 用 ChildNetStatsContext，互不影响）。
	NetStatsContext = ReplicationSystemInternal->GetNetTypeStats().GetNetStatsContext();

	const bool bIsIrisPushModelEnabled = IsIrisPushModelEnabled();
	
	FNetBitArray TempObjectsToPoll;
	
	// Prepare bitarray with objects to poll, filtering out all fully push based that we can skip.
	// 优化路径：把"完全 push-based 且未脏未 GC 影响"的对象批量从 poll 列表里屏蔽。
	// 节省：每个被屏蔽的对象省掉一个 TFunction 调用 + 一次 PushModelPollObject 内部分支判定。
	const bool bUseFilteredObjectsConsideredForPolling = CVars::bFilterOutNonDirtyPushBasedObjects && bIsIrisPushModelEnabled;
	if (bUseFilteredObjectsConsideredForPolling)
	{
		IRIS_PROFILER_SCOPE(FilterObjectsConsideredForPolling);

		using StorageWordType = UE::Net::FNetBitArrayBase::StorageWordType;

		TempObjectsToPoll.Init(InObjectsConsideredForPolling.GetNumBits());

		const FNetBitArrayView ObjectsWithFullPushBasedDirtiness = LocalNetRefHandleManager.GetObjectsWithFullPushBasedDirtiness();

		const StorageWordType* ObjectsConsideredForPollingData = InObjectsConsideredForPolling.GetData();
		const StorageWordType* ObjectsWithFullPushBasedDirtinessData = ObjectsWithFullPushBasedDirtiness.GetData();
		const StorageWordType* DirtyObjectsThisFrameData = DirtyObjectsThisFrame.GetData();
		const StorageWordType* AccumulatedDirtyObjectsData = AccumulatedDirtyObjects.GetData();
		const StorageWordType* GarbageCollectionAffectedObjectsData = GarbageCollectionAffectedObjects.GetData();
		StorageWordType* TempObjectsToPollData = TempObjectsToPoll.GetData();

		// Build a bitarray for all objects that we need to poll, masking off all objects we can skip (fully push based, not dirty, and not affected by GC).
		// Assumes that "new"-objects are properly marked as dirty.
		// 关键位运算（按 word 处理，提速 32 倍）：
		//   skippable = full_push & ~(dirty_this_frame | accumulated_dirty | gc_affected)
		//   to_poll   = considered & ~skippable
		// 即"完全 push 但本帧未脏、累积未脏、未受 GC 影响" → 跳过；否则保留。
		// 注：新对象（NewObject）必须由其它机制（DirtyNetObjectTracker）标脏，否则会被错误跳过。
		for (uint32 WordIt = 0U; WordIt < InObjectsConsideredForPolling.GetNumWords(); ++WordIt)
		{
			const FNetBitArrayBase::StorageWordType SkippableObjects = ObjectsWithFullPushBasedDirtinessData[WordIt] & ~(DirtyObjectsThisFrameData[WordIt] | AccumulatedDirtyObjectsData[WordIt] | GarbageCollectionAffectedObjectsData[WordIt]);
			TempObjectsToPollData[WordIt] = ObjectsConsideredForPollingData[WordIt] & ~SkippableObjects;
		}
	}

	// Pick filtered or non-filtered polling list.
	const FNetBitArrayView ObjectsConsideredForPolling = bUseFilteredObjectsConsideredForPolling ? UE::Net::MakeNetBitArrayView(TempObjectsToPoll, FNetBitArrayBase::NoResetNoValidate) : InObjectsConsideredForPolling;

	// We need to split the work somewhat evenly across multiple tasks, so we define a Chunk as
	// a number of contiguous words (each 32-bit word represents 32 objects), so 16 words per chunk 
	// is 64-bytes and should fill out a whole cache line on standard platforms.
	// The Chunk size must be small enough to result in at least one chunk per task, otherwise fall back to synchronous
	// 计算并行可行性：必须每个 task 至少有 1 个 chunk 可处理，否则回退同步（避免 task 启动开销 > 实际工作）。
	const uint32 NumPollingTasks = CVars::GNumPollingTasks;
	const uint32 BitsPerChunk = CVars::GNumWordsPerChunk * FNetBitArray::WordBitCount;
	const bool bEachTaskHasAChunkOfWork = (BitsPerChunk * NumPollingTasks) < ObjectsConsideredForPolling.GetNumBits();
	
	if (ReplicationSystemInternal->AreParallelTasksAllowed() && bEachTaskHasAChunkOfWork && NumPollingTasks > 0)
	{
		// 进入并行阶段：会改写 ReplicationSystemInternal 的 IsInParallelPhase 标志，下游 check 据此判定。
		ReplicationSystemInternal->StartParallelPhase();

		TArray<UE::Tasks::TTask<void>> TasksToComplete;
		TArray<FReplicationPollTaskData> TaskData;
		// 注意 TaskData 在外面预分配——lambda 引用其元素时必须确保 TaskData 不在 task 完成前 reallocate。
		// 这里用 Init(...) 一次性分配 NumPollingTasks 个，且后续未 push_back，安全。
		TaskData.Init(FReplicationPollTaskData(ObjectsConsideredForPolling), NumPollingTasks);

		for (uint32 CurrentTaskIndex = 0; CurrentTaskIndex < NumPollingTasks; CurrentTaskIndex++)
		{
			IRIS_PROFILER_SCOPE(PrepareAndDispatchTask);
			
			FReplicationPollTaskData& Data = TaskData[CurrentTaskIndex];
			Data.bUsePushModel = bIsIrisPushModelEnabled;//If pushmodel is disabled, call ForcePollObject instead
			Data.NetTypeStats = &ReplicationSystemInternal->GetNetTypeStats();
			Data.CurrentTaskIndex = CurrentTaskIndex;
			Data.BitsPerChunk = BitsPerChunk;
			Data.ChunkStride = NumPollingTasks * BitsPerChunk;//How many bits should the task skip to get to the next Chunk it owns?

			{
				IRIS_PROFILER_SCOPE(UObjectPoller_DispatchTask); 
				
				TasksToComplete.Add(UE::Tasks::Launch(UE_SOURCE_LOCATION, [this, &Data]()
					{
						FReplicationPollTask NewTask = FReplicationPollTask(this, Data, ReplicationSystemInternal);
						NewTask.DoTask();
					}
				));
			}
		}
	
		//This can also run some of the task list on the current thread (GameThread), but only ones from TaskToComplete
		// UE::Tasks::Wait 在等待期间允许当前线程（GameThread）也参与执行 TasksToComplete 列表中的 task，
		// 充分利用 GameThread 等待时间，但只会执行我们刚提交的这些 task，不会"窃取"其它无关 task。
		UE::Tasks::Wait(TasksToComplete);

		ReplicationSystemInternal->StopParallelPhase();
	
		//Combine the PollStats from individual tasks into the shared one
		// 单线程串行合并 PollStats（task 间已无写竞争，安全）。
		for (const FReplicationPollTaskData& ThisTaskData : TaskData)
		{
			PollStats.Accumulate(ThisTaskData.PollStats);
		} 
	}
	else
	{
		//Synchronous, non-task based polling of all objects considered for polling
		// 同步路径：单线程 ForAllSetBits。仍可获得不错性能（PushModel 路径已极轻）。
		if (IsIrisPushModelEnabled())
		{
			IRIS_PROFILER_SCOPE(PollAndCopyPushBased);

			ObjectsConsideredForPolling.ForAllSetBits([this](FInternalNetRefIndex Objectindex)
			{
				PushModelPollObject(Objectindex, NetStatsContext, PollStats);
			});
		}
		else
		{
			IRIS_PROFILER_SCOPE(ForcePollAndCopy);

			ObjectsConsideredForPolling.ForAllSetBits([this](FInternalNetRefIndex Objectindex)
			{
				ForcePollObject(Objectindex, NetStatsContext, PollStats);
			});
		}
	}

	NetStatsContext = nullptr;
}

void FObjectPoller::PollAndCopySingleObject(FInternalNetRefIndex ObjectIndex)
{
	// 单对象路径：仅由 GameThread 同步调用（如 ForceNetUpdate）。
	FDirtyObjectsAccessor DirtyObjectsAccessor(ReplicationSystemInternal->GetDirtyNetObjectTracker());
	DirtyObjectsThisFrame = DirtyObjectsAccessor.GetDirtyNetObjects();

	//We don't run this PollAndCopySingleObject during the parallel Poll phase, so we can use the parent context NetStatsContext and PollStats
	check(!ReplicationSystemInternal->GetIsInParallelPhase());

	IRIS_PROFILER_SCOPE_VERBOSE(ForcePollAndCopy);
	ForcePollObject(ObjectIndex, NetStatsContext, PollStats);

	// Clear ref to locked dirty bit array
	// 析构 DirtyObjectsAccessor 之前清空成员，防止其他方法误用过期视图。
	DirtyObjectsThisFrame = FNetBitArrayView();
	NetStatsContext = nullptr;
}

void FObjectPoller::ForcePollObject(FInternalNetRefIndex ObjectIndex, FNetStatsContext* InNetStatsContext, FPreUpdateAndPollStats& InPollStats)
{
	FNetRefHandleManager::FReplicatedObjectData& ObjectData = LocalNetRefHandleManager.GetReplicatedObjectDataNoCheck(ObjectIndex);
	if (UNLIKELY(ObjectData.InstanceProtocol == nullptr))
	{
		// 对象已停止复制（ProtocolInstance 已释放）但位图尚未清零的边界情况，直接返回。
		return;
	}

	// We always poll all states here.
	// ForcePoll 总是 poll 全部状态——重置 bWantsFullPoll 标志（下一帧不必再走 force 全 poll）。
	ObjectData.bWantsFullPoll = 0U;

	// Poll properties if the instance protocol requires it
	// 仅当 InstanceProtocol 标记 NeedsPoll 时才真正调用 PollAndCopyPropertyData——
	// 例如纯 Function-only 的对象（无属性）或 ShrinkWrap blob 类对象不需要 poll。
	if (EnumHasAnyFlags(ObjectData.InstanceProtocol->InstanceTraits, EReplicationInstanceProtocolTraits::NeedsPoll))
	{
		IRIS_PROFILER_PROTOCOL_NAME_CONDITIONAL(ObjectData.Protocol->DebugName->Name, CVars::bEnableVerbosePollProfiling);
		UE_NET_IRIS_STATS_TIMER(Timer, InNetStatsContext);
		UE_NET_TRACE_POLL_OBJECT_SCOPE(ObjectData.RefHandle, Timer);

		// GC 影响标志：上次 GC 后该对象是否需要刷新缓存的 ObjectReference。
		const bool bIsGCAffectedObject = GarbageCollectionAffectedObjects.GetBit(ObjectIndex);
		GarbageCollectionAffectedObjects.ClearBit(ObjectIndex);

		// If this object has been around for a garbage collect and it has object references we must make sure that we update all cached object references
		EReplicationFragmentPollFlags PollOptions = EReplicationFragmentPollFlags::PollAllState;
		PollOptions |= CVars::bEnableVerbosePollProfiling ? EReplicationFragmentPollFlags::EnableVerboseProfiling : EReplicationFragmentPollFlags::None;
		PollOptions |= bIsGCAffectedObject ? EReplicationFragmentPollFlags::ForceRefreshCachedObjectReferencesAfterGC : EReplicationFragmentPollFlags::None;

		const bool bWasAlreadyDirty = DirtyObjectsThisFrame.IsBitSet(ObjectIndex);
		// 真实的 poll & copy：fragment 比对外部源缓冲 vs 内部 mirror，发现差异则更新内部并返回 true。
		const bool bPollFoundDirty = FReplicationInstanceOperations::PollAndCopyPropertyData(ObjectData.InstanceProtocol, PollOptions);
		if (bWasAlreadyDirty || bPollFoundDirty)
		{
			UE_NET_IRIS_STATS_ADD_TIME_AND_COUNT_FOR_OBJECT(Timer, Poll, ObjectIndex);

			DirtyObjectsToQuantize.SetBit(ObjectIndex);
			DirtyObjectsThisFrame.SetBit(ObjectIndex);
		}
		else
		{
			// 没找到任何变化——本次 poll 是"浪费"。Stats 系统记一次无效 poll 用于 profile/优化决策。
			UE_NET_IRIS_STATS_ADD_TIME_AND_COUNT_FOR_OBJECT_AS_WASTE(Timer, Poll, ObjectIndex);
			UE_NET_TRACE_POLL_OBJECT_IS_WASTE();
		}
		++InPollStats.PolledObjectCount;
	}
	else
	{
		// 不需要 poll 的对象：直接标脏，由下游 quantize 阶段处理。
		DirtyObjectsToQuantize.SetBit(ObjectIndex);
		DirtyObjectsThisFrame.SetBit(ObjectIndex);
	}
}

void FObjectPoller::PushModelPollObject(FInternalNetRefIndex ObjectIndex, FNetStatsContext* InNetStatsContext, FPreUpdateAndPollStats& InPollStats)
{
	FNetRefHandleManager::FReplicatedObjectData& ObjectData = LocalNetRefHandleManager.GetReplicatedObjectDataNoCheck(ObjectIndex);
	const FReplicationInstanceProtocol* InstanceProtocol = ObjectData.InstanceProtocol;
	if (UNLIKELY(InstanceProtocol == nullptr))
	{
		return;
	}

	const EReplicationInstanceProtocolTraits InstanceTraits = InstanceProtocol->InstanceTraits;
	const bool bNeedsPoll = EnumHasAnyFlags(InstanceTraits, EReplicationInstanceProtocolTraits::NeedsPoll);

	// 是否脏：累积脏 OR 本帧脏。
	bool bIsDirtyObject = AccumulatedDirtyObjects.GetBit(ObjectIndex) || DirtyObjectsThisFrame.GetBit(ObjectIndex);

	if (bIsDirtyObject)
	{
		// 提前标脏 quantize 列表（即使下面决定不 poll，这些对象也需要 quantize）。
		DirtyObjectsToQuantize.SetBit(ObjectIndex);
		DirtyObjectsThisFrame.SetBit(ObjectIndex);
	}

	const bool bIsGCAffectedObject = GarbageCollectionAffectedObjects.GetBit(ObjectIndex);
	GarbageCollectionAffectedObjects.ClearBit(ObjectIndex);

	// Early out if the instance does not require polling
	// 不需要 poll 的对象（典型：ShrinkWrap blob、Function-only RPC 对象）：到此结束。
	if (!bNeedsPoll)
	{
		return;
	}

	// Does the object need to poll all states once.
	// bWantsFullPoll：StartReplicating 后第一次 poll 必须 fullPoll（让所有状态进入 ChangeMask）。
	const bool bWantsFullPoll = ObjectData.bWantsFullPoll;
	ObjectData.bWantsFullPoll = 0U;
	
	// If the object is fully push model we only need to poll it if it's dirty, unless it's a new object or was garbage collected.
	bool bPollFoundDirty = false;
	// 分支 A：对象的所有 fragment 都是 fully-push-based（修改属性时主动 MARK_PROPERTY_DIRTY）。
	if (EnumHasAnyFlags(InstanceTraits, EReplicationInstanceProtocolTraits::HasFullPushBasedDirtiness))
	{
		if (bIsDirtyObject || bWantsFullPoll)
		{
			IRIS_PROFILER_PROTOCOL_NAME_CONDITIONAL(ObjectData.Protocol->DebugName->Name, CVars::bEnableVerbosePollProfiling);
			UE_NET_IRIS_STATS_TIMER(Timer, NetStatsContext);
			UE_NET_TRACE_POLL_OBJECT_SCOPE(ObjectData.RefHandle, Timer);

			// We need to do a poll if object is marked as dirty
			// 优化：标准帧只 poll dirty 状态（PollDirtyState）；首次 / fullPoll 需求时才走 PollAllState。
			EReplicationFragmentPollFlags PollOptions = bUsePerPropertyDirtyTracking && !bWantsFullPoll ? EReplicationFragmentPollFlags::PollDirtyState : EReplicationFragmentPollFlags::PollAllState;
			PollOptions |= CVars::bEnableVerbosePollProfiling ? EReplicationFragmentPollFlags::EnableVerboseProfiling : EReplicationFragmentPollFlags::None;
			PollOptions |= bIsGCAffectedObject ? EReplicationFragmentPollFlags::ForceRefreshCachedObjectReferencesAfterGC : EReplicationFragmentPollFlags::None;
			bPollFoundDirty = FReplicationInstanceOperations::PollAndCopyPropertyData(InstanceProtocol, EReplicationFragmentTraits::None, PollOptions);
			++InPollStats.PolledObjectCount;

			UE_NET_IRIS_STATS_ADD_TIME_AND_COUNT_FOR_OBJECT(Timer, Poll, ObjectIndex);
		}
		else if (bIsGCAffectedObject)
		{
			IRIS_PROFILER_PROTOCOL_NAME_CONDITIONAL(ObjectData.Protocol->DebugName->Name, CVars::bEnableVerbosePollProfiling);
			UE_NET_IRIS_STATS_TIMER(Timer, NetStatsContext);
			UE_NET_TRACE_POLL_OBJECT_SCOPE(ObjectData.RefHandle, Timer);

			// If this object might have been affected by GC, only refresh cached references
			// GC 路径：对象未脏但 GC 后某些 ObjectReference 可能被替换/失效，仅刷新 cached references。
			bPollFoundDirty = FReplicationInstanceOperations::PollAndCopyObjectReferences(InstanceProtocol, EReplicationFragmentTraits::None);
			++InPollStats.PolledReferencesObjectCount;

			UE_NET_IRIS_STATS_ADD_TIME_AND_COUNT_FOR_OBJECT(Timer, Poll, ObjectIndex);
		}
		else
		{
			// fully-push 且未脏未 GC 影响 → 完全跳过。这是 PushModel 的核心收益。
			++PollStats.SkippedObjectCount;
		}
	}
	else
	// 分支 B：对象有混合 push/legacy fragment，必须 poll 至少 legacy 部分。
	{
		IRIS_PROFILER_PROTOCOL_NAME_CONDITIONAL(ObjectData.Protocol->DebugName->Name, CVars::bEnableVerbosePollProfiling);
		UE_NET_IRIS_STATS_TIMER(Timer, NetStatsContext);
		UE_NET_TRACE_POLL_OBJECT_SCOPE(ObjectData.RefHandle, Timer);

		// If the object has fragments with pushed based properties, and is not marked dirty and object is affected by GC we need to make sure that we refresh cached references for all fragments with push based properties
		// 子情况 B1：未脏 + GC 影响 + 有 push fragment + 含 ObjectReference → 单独刷新 push fragment 的 references。
		const bool bHasPushBasedFragments = EnumHasAnyFlags(InstanceTraits, EReplicationInstanceProtocolTraits::HasPushBasedDirtiness);
		const bool bHasObjectReferences = EnumHasAnyFlags(InstanceTraits, EReplicationInstanceProtocolTraits::HasObjectReference);
		const bool bNeedsRefreshOfCachedObjectReferences = ((!(bWantsFullPoll | bIsDirtyObject)) & bIsGCAffectedObject & bHasPushBasedFragments & bHasObjectReferences);
		if (bNeedsRefreshOfCachedObjectReferences)
		{
			// Only states which has full push based dirtiness need to be updated as the other states will be at least partially polled anyway.
			const EReplicationFragmentTraits RequiredTraits = EReplicationFragmentTraits::HasFullPushBasedDirtiness;
			bPollFoundDirty = FReplicationInstanceOperations::PollAndCopyObjectReferences(InstanceProtocol, RequiredTraits);
			++InPollStats.PolledReferencesObjectCount;
		}

		// We currently cannot trust changemask for multi-owner instances so we need to poll.
		// MultiObjectInstance（如 SubObject 共享同一 InstanceProtocol）：保守起见每帧都 fullPoll。
		const bool bIsMultiOwnerInstance = EnumHasAnyFlags(InstanceTraits, EReplicationInstanceProtocolTraits::IsMultiObjectInstance);

		// If this object has been around for a garbage collect and it has object references we must make sure that we update all cached object references 
		// PollDirtyState vs PollAllState：
		//   - 启用 per-property dirty tracking + 非首次 + 非 multi-owner → PollDirtyState（仅按 ChangeMask poll）；
		//   - 否则 PollAllState（兜底全 poll）。
		EReplicationFragmentPollFlags PollOptions = bUsePerPropertyDirtyTracking && !bWantsFullPoll && !bIsMultiOwnerInstance ? EReplicationFragmentPollFlags::PollDirtyState : EReplicationFragmentPollFlags::PollAllState;
		PollOptions |= CVars::bEnableVerbosePollProfiling ? EReplicationFragmentPollFlags::EnableVerboseProfiling : EReplicationFragmentPollFlags::None;
		PollOptions |= bIsGCAffectedObject ? EReplicationFragmentPollFlags::ForceRefreshCachedObjectReferencesAfterGC : EReplicationFragmentPollFlags::None;

		// If the object is not new or dirty at this point we only need to poll non-fully push based fragments as we know that fully pushed based states have not been modified or have already had their 
		// 对未脏的对象只 poll legacy fragment（fully-push 的部分已通过 dirty tracker 处理）。
		const EReplicationFragmentTraits ExcludeTraits = (bIsDirtyObject || bWantsFullPoll || bIsMultiOwnerInstance) ? EReplicationFragmentTraits::None : EReplicationFragmentTraits::HasFullPushBasedDirtiness;
		bPollFoundDirty |= FReplicationInstanceOperations::PollAndCopyPropertyData(InstanceProtocol, ExcludeTraits, PollOptions);
		++InPollStats.PolledObjectCount;

		if (bPollFoundDirty)
		{
			UE_NET_IRIS_STATS_ADD_TIME_AND_COUNT_FOR_OBJECT(Timer, Poll, ObjectIndex);

			DirtyObjectsToQuantize.SetBit(ObjectIndex);
			DirtyObjectsThisFrame.SetBit(ObjectIndex);
		}
		else
		{
			UE_NET_IRIS_STATS_ADD_TIME_AND_COUNT_FOR_OBJECT_AS_WASTE(Timer, Poll, ObjectIndex);
			UE_NET_TRACE_POLL_OBJECT_IS_WASTE();
		}
	}
}

} // end namespace UE::Net::Private

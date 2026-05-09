// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================================================================
// 文件作用：FObjectPoller —— Iris Polling 子模块的核心类。
// 职责：在 PreSendUpdate 阶段并行 / 同步地遍历"应轮询"对象集合，从游戏世界拉脏属性到网络内部 buffer。
//
// 调用链（详见 Iris_Architecture.md §4 帧循环）：
//   FObjectReplicationBridge::PreSendUpdate
//     → BuildPollList（FObjectPollFrequencyLimiter 决定本帧需轮询的对象集合）
//     → PreUpdate（用户回调，IObjectReplicationBridgeQueryDelegate）
//     → FinalizeDirty
//     → PollAndCopyObjects(ObjectsConsideredForPolling)  ← 本类入口
//
// 工作模式（运行时由 CVar net.Iris.NumPollingTasks 控制）：
//   - 同步模式（NumPollingTasks=0 或对象数太少）：单线程 ForAllSetBits 直接遍历；
//   - 并行模式：把对象位图按 "Chunk = NumWordsPerChunk*32 个对象" 切片，N 个 Task 各拿"间隔 N*Chunk"
//               的若干 chunk 处理。这种"交错切片"避免连续 0 段导致的负载不均衡。
//
// 两条实际 poll 路径（per-object）：
//   - PushModelPollObject：仅 Iris PushModel 启用时使用。如对象 fully-push-based 且未脏 → 完全跳过；
//                          否则只 poll 标脏的属性（PollDirtyState）。
//   - ForcePollObject：兜底全 poll（PollAllState）。
//
// 错误处理：
//   - 用户回调（PreUpdate）抛异常会破坏 GameState。Iris 不专门捕获——交给上层 try/catch 或 PIE 错误流程；
//   - GC 影响的对象（GarbageCollectionAffectedObjects）：在 poll 中检测并强制刷新缓存的 ObjectReference。
// =============================================================================================================================

#pragma once

#include "Net/Core/NetBitArray.h"
#include "Iris/Core/NetChunkedArray.h"

// Forward declarations
class UObjectReplicationBridge;

namespace UE::Net
{
	class FNetRefHandle;

	namespace Private
	{
		typedef uint32 FInternalNetRefIndex;

		class FReplicationSystemInternal;
		class FNetRefHandleManager;
		class FNetStatsContext;
	}
}

namespace UE::Net::Private
{

/** Class that holds the required information needed to execute the poll phase on one or multiple replicated objects. */
// FObjectPoller：构造时绑定 ReplicationSystem 与 Bridge；可重复多次调用 PollAndCopyObjects。
// 不持有对象生命周期——所有内部引用都来自 ReplicationSystemInternal。
class FObjectPoller
{
	friend class FReplicationPollTask;

public:

	/** Holds statistics on how the polling went */
	// 本帧 poll 统计：用于 CSV/Stats 输出。并行模式下，每 task 独立累加，最后由 GameThread 用 Accumulate 合并。
	struct FPreUpdateAndPollStats
	{
		uint32 PreUpdatedObjectCount = 0;       // 收到 PreUpdate 回调的对象数
		uint32 PolledObjectCount = 0;           // 实际执行 PollAndCopyPropertyData 的对象数
		uint32 SkippedObjectCount = 0;          // PushModel 下被跳过的对象数（fully-push 且未脏）
		uint32 PolledReferencesObjectCount = 0; // GC 后仅刷新 ObjectReference 缓存的对象数

		void Accumulate(const FPreUpdateAndPollStats& StatsToAdd)
		{
			PreUpdatedObjectCount += StatsToAdd.PreUpdatedObjectCount;
			PolledObjectCount += StatsToAdd.PolledObjectCount;
			PolledReferencesObjectCount += StatsToAdd.PolledReferencesObjectCount;
			// 注意：原代码未累加 SkippedObjectCount —— 推测是有意只统计干活的，跳过的不重要。
		}
	};

	struct FInitParams
	{
		FReplicationSystemInternal* ReplicationSystemInternal = nullptr;
		UObjectReplicationBridge* ObjectReplicationBridge = nullptr;
	};

public:

	FObjectPoller(const FInitParams& InitParams);

	const FPreUpdateAndPollStats& GetPollStats() const { return PollStats; }

	/** Poll all the objects whose bit index is set in the array and copy any dirty data into ReplicationState buffers*/
	// 主入口：传入"本帧需 poll"的位图，内部决定同步 / 并行；输出脏属性已拷到 ReplicationState 内部 buffer。
	// 本帧首次/末次调用都会 lock/unlock DirtyObjectsThisFrame（见 cpp）。
	void PollAndCopyObjects(const FNetBitArrayView& ObjectsConsideredForPolling);

	/** Poll a single replicated object */
	// 单对象 poll（用于 ForceNetUpdate 等同步路径）。仅在非并行阶段调用。
	void PollAndCopySingleObject(FInternalNetRefIndex ObjectIndex);

private:

	/** Polls an object in every circumstance */
	// 强制全 poll（不论是否 PushModel）。设置 PollAllState；标脏 DirtyObjectsToQuantize / DirtyObjectsThisFrame。
	void ForcePollObject(FInternalNetRefIndex ObjectIndex, FNetStatsContext* InNetStatsContext, FPreUpdateAndPollStats& InPollStats);

	/** Polls an object only if it is required or considered dirty */
	// PushModel 路径：fully-push 且未脏的对象直接跳过；只对脏对象做 PollDirtyState；GC 影响的对象单独刷 ObjectReference。
	void PushModelPollObject(FInternalNetRefIndex ObjectIndex, FNetStatsContext* InNetStatsContext, FPreUpdateAndPollStats& InPollStats);

private:

	UObjectReplicationBridge* ObjectReplicationBridge;
	FReplicationSystemInternal* ReplicationSystemInternal;

	// 中央对象表（提供 ReplicatedObjectData / 脏位图 / FullPushBased 位图）。
	FNetRefHandleManager& LocalNetRefHandleManager;
	// 当前 task 的 NetStatsContext（同步路径下从 ReplicationSystemInternal 获取，并行路径下每 task 独立）。
	FNetStatsContext* NetStatsContext = nullptr;
	// 已注册对象的 UObject 弱引用数组（PreUpdate 回调要传给用户）。
	const TNetChunkedArray<TObjectPtr<UObject>>& ReplicatedInstances;

	// 累积脏位图：包含从上次发送至今所有曾被标脏的对象。用于 PushModel 判定"是否需要 poll"。
	const FNetBitArrayView AccumulatedDirtyObjects;

	// "脏对象需 quantize"位图：被本类按对象索引置位，下游 QuantizeDirtyStateData 阶段消费。
	FNetBitArrayView DirtyObjectsToQuantize;
	// 本帧脏对象位图（FDirtyObjectsAccessor RAII 锁定）。仅在 PollAndCopyObjects 调用期间有效。
	FNetBitArrayView DirtyObjectsThisFrame;
	// 受 GC 影响的对象（Bridge 在 GC 后填入）。Poll 时需要刷新 ObjectReference 缓存。
	FNetBitArrayView GarbageCollectionAffectedObjects;

	FPreUpdateAndPollStats PollStats;

	// 是否启用 per-property 脏跟踪（FGlobalDirtyNetObjectTracker::IsUsingPerPropertyDirtyTracking）。
	// 影响 PollDirtyState vs PollAllState 的选择。
	bool bUsePerPropertyDirtyTracking = false;
};

} // end namespace UE::Net::Private


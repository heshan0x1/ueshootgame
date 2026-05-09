// Copyright Epic Games, Inc. All Rights Reserved.

// =====================================================================================
// 文件：DirtyNetObjectTracker.h
// 模块：Iris / ReplicationSystem（辅助 & 周边）
// 功能：本 RS 内的"脏对象索引位图"维护，并对接 NetCore 的 GlobalDirtyNetObjectTracker。
//
// 三方关系：
//   ┌──────────────────────────────┐  PreSendUpdate 早期 Apply
//   │ FGlobalDirtyNetObjectTracker │ ──────────────────────────┐
//   │  (NetCore 跨 RS / 跨系统)    │                            │
//   └──────────────┬───────────────┘                            ▼
//                  │ MarkNetObjectDirty(FNetHandle, …)   ┌─────────────────────────┐
//                  │                                     │ FDirtyNetObjectTracker  │
//   ┌──────────────┴───────────────┐  push 转发 (FNetHandle│  (per-RS, 内部稠密索引) │
//   │ LegacyPushModel              │ ──→ RepIndex 范围)   │  · DirtyNetObjects       │
//   │  (MARK_PROPERTY_DIRTY 旧宏)  │                     │  · AccumulatedDirtyNetObjects │
//   └──────────────────────────────┘                     │  · ForceNetUpdateObjects │
//                                                        └────────────┬────────────┘
//                                                                     │
//                                                          ReplicationSystem.cpp
//                                                          → Filtering / Polling /
//                                                            Quantize / Writer 驱动
//
// 三个 BitArray 语义：
//   * DirtyNetObjects            —— 本帧脏（每帧 ReconcilePolledList 后清 0）。
//   * AccumulatedDirtyNetObjects —— 跨帧累积脏（直到对象成功被 Polled 才会清）。
//   * ForceNetUpdateObjects      —— 调用 ForceNetUpdate(...) 标记的"必须本帧轮询"。
//
// 帧循环对接（FReplicationSystemImpl）：
//   - StartPreSendUpdate            → UpdateDirtyNetObjects   （拉 Global → DirtyNetObjects）
//   - Bridge.PreSendUpdate (Poll)   → UpdateAccumulatedDirtyList
//   - PostSendUpdate (Polled 列表)  → ReconcilePolledList     （清掉已成功 poll 的对象的脏标记）
//
// 线程安全：
//   * FDirtyObjectsAccessor RAII 在 Scope 内 Lock 本表（禁止外部 Mark），
//     供 Filtering/Polling 阶段以"只读"方式遍历 DirtyNetObjects。
//   * Mark/ForceNetUpdate 仅在 AllowExternalAccess 状态下被调用，
//     `bIsExternalAccessAllowed` 由 UE_NET_THREAD_SAFETY_CHECK 保护。
//
// 全局函数：
//   * MarkNetObjectStateDirty(RSId, InternalIndex)：来自 LegacyPushModel 转发或
//     系统内部 Bridge/Conditionals 等显式标脏。
//   * ForceNetUpdate(RSId, InternalIndex)：来自 UReplicationSystem::ForceNetUpdate。
// =====================================================================================

#pragma once

#include "Logging/LogMacros.h"

#include "Net/Core/NetBitArray.h"
#include "Net/Core/DirtyNetObjectTracker/GlobalDirtyNetObjectTracker.h"

#include "Iris/IrisConfig.h"
#include "Iris/Core/IrisCsv.h"

namespace UE::Net::Private
{
	class FNetRefHandleManager;
	class FDirtyObjectsAccessor;
	
	// 中文：每对象在 RS 内部稠密索引的别名（FNetRefHandleManager 分配）。
	typedef uint32 FInternalNetRefIndex;
}

#ifndef UE_NET_DIRTYOBJECTTRACKER_LOG_COMPILE_VERBOSITY
// Don't compile verbose logs in Shipping builds	
// 中文：Shipping 关闭 Verbose 日志，开发版打开（便于排查脏标记泄漏 / 误标记）。
#if UE_BUILD_SHIPPING
#	define UE_NET_DIRTYOBJECTTRACKER_LOG_COMPILE_VERBOSITY Log
#else
#	define UE_NET_DIRTYOBJECTTRACKER_LOG_COMPILE_VERBOSITY All
#endif
#endif

IRISCORE_API DECLARE_LOG_CATEGORY_EXTERN(LogIrisDirtyTracker, Log, UE_NET_DIRTYOBJECTTRACKER_LOG_COMPILE_VERBOSITY);

namespace UE::Net::Private
{

// 中文：全局入口——按 RS 找到对应 FDirtyNetObjectTracker 并标脏。
// 主要调用方：UReplicationSystem::MarkDirty、LegacyPushModel 桥接转发。
IRISCORE_API void MarkNetObjectStateDirty(uint32 ReplicationSystemId, FInternalNetRefIndex NetObjectIndex);
// 中文：等价于 MarkNetObjectStateDirty + 设置 ForceNetUpdate 位（强制本帧 Poll）。
IRISCORE_API void ForceNetUpdate(uint32 ReplicationSystemId, FInternalNetRefIndex NetObjectIndex);

// 中文：FDirtyNetObjectTracker::Init 入参。
struct FDirtyNetObjectTrackerInitParams
{
	const FNetRefHandleManager* NetRefHandleManager = nullptr;
	uint32 ReplicationSystemId = 0;
	// 中文：初始 BitArray 大小（位数）。当 NetRefHandleManager 触发
	// MaxInternalNetRefIndexIncreased 时自动扩展。
	uint32 MaxInternalNetRefIndex = 0;
};

class FDirtyNetObjectTracker
{
public:
	FDirtyNetObjectTracker();
	~FDirtyNetObjectTracker();

	// 中文：注册到 GlobalDirtyNetObjectTracker（拿 PollHandle）+ 订阅
	// NetRefHandleManager 的"最大索引扩容"事件 + 初始化三个 BitArray。
	void Init(const FDirtyNetObjectTrackerInitParams& Params);
	void Deinit();

	/** Returns true if this dirty tracker can be used by the replication system */
	bool IsInit() const { return NetRefHandleManager != nullptr; }

	/** Update dirty objects with the set of globally marked dirty objects. */
	// 中文：从 Global 拉取本帧/上帧累积的脏 NetHandle，转换成 InternalIndex
	// 后并入 DirtyNetObjects；做范围/scope 过滤。
	void UpdateDirtyNetObjects();

	/* Update dirty objects from the global list and then prevent future modifications to that list until it is reset. */
	// 中文：UpdateDirtyNetObjects 后立即锁定 Global 列表，防止本帧后续误标。
	// 用于"已经过了 Mark 期"的阶段做最终一致性。
	void UpdateAndLockDirtyNetObjects();

	/** Add all the current frame dirty objects set into the accumulated list */
	// 中文：把 DirtyNetObjects 整列 OR 进 AccumulatedDirtyNetObjects。
	// 在 Bridge.PreSendUpdate（Poll 之前）调用——保证累计列表覆盖所有
	// 本帧 Mark 但 PreSendUpdate 之后又被新一轮 Mark 的对象。
	void UpdateAccumulatedDirtyList();

	/** Set safety permissions so no one can write in the bit array via the public methods */
	void LockExternalAccess();

	/** Release safety permissions and allow to write in the bit array via the public methods */
	void AllowExternalAccess();

	/** Reset the global list and look at the final polled list and clear any flags for objects that got polled */
	// 中文：PostSendUpdate 阶段调用——把"成功 Poll 过的对象"从
	// Accumulated/ForceNetUpdate 中清除（AndNot），并清空本帧 DirtyNetObjects。
	// 同时若本 RS 是 Global 单一 Poller 则触发一次 Reset。
	void ReconcilePolledList(const FNetBitArrayView& ObjectsPolled);

#if UE_NET_IRIS_CSV_STATS
	void ReportCSVStats();
#endif

	/** Returns the list of objects that are dirty this frame or were dirty in previous frames but not cleaned up at that time. */
	const FNetBitArrayView GetAccumulatedDirtyNetObjects() const { return MakeNetBitArrayView(AccumulatedDirtyNetObjects); }

	/** Returns the list of objects who asked to force a replication this frame */
	FNetBitArrayView GetForceNetUpdateObjects() { return MakeNetBitArrayView(ForceNetUpdateObjects); }
	const FNetBitArrayView GetForceNetUpdateObjects() const { return MakeNetBitArrayView(ForceNetUpdateObjects); }

private:
	// 中文：以下两个全局 helper 直接 Mark/ForceNetUpdate 私有方法，因此声明友元。
	friend IRISCORE_API void MarkNetObjectStateDirty(uint32 ReplicationSystemId, FInternalNetRefIndex NetObjectIndex);
	friend IRISCORE_API void ForceNetUpdate(uint32 ReplicationSystemId, FInternalNetRefIndex NetObjectIndex);

	// 中文：本帧脏列表的"读"接口仅通过 RAII Accessor 暴露，避免裸引用。
	friend FDirtyObjectsAccessor;

	using StorageType = FNetBitArrayView::StorageWordType;
	static constexpr uint32 StorageTypeBitCount = FNetBitArrayView::WordBitCount;

	// 中文：BitArray 容量调整（NetRefHandleManager 扩容时同步）。
	void SetNetObjectListsSize(FInternalNetRefIndex NewMaxInternalIndex);
	void OnMaxInternalNetRefIndexIncreased(FInternalNetRefIndex NewMaxInternalIndex);

	// 中文：内部裸 Mark/ForceNetUpdate（由全局 helper 转发）。
	void MarkNetObjectDirty(FInternalNetRefIndex NetObjectIndex);
	void ForceNetUpdate(FInternalNetRefIndex NetObjectIndex);
	// 中文：把 Global 列表中的脏 Handle 翻译成 InternalIndex 后并入 DirtyNetObjects。
	// 若 GlobalTracker 启用了 PerProperty 模式，还会顺带把脏属性 RepIndex 透传到
	// Push-based fragment 的 MemberPollMask（详见 .cpp）。
	void ApplyGlobalDirtyObjectList();

	/**
	 * Applies the dirty state from the global dirty object tracker.
	 * Then if this is the only poller of global dirty state, resets the global dirty state.
	 * If there are multiple pollers of global dirty state (multiple replication systems),
	 * the global state can't be reset until all pollers have gathered it. So we set the
	 * bShouldResetPolledGlobalDirtyTracker flag which will attempt another reset in ReconcilePolledList,
	 * which is called in PostSendUpdate after other pollers have had a chance to gather.
	 *
	 * 中文（多 RS 协作的关键点）：
	 *   * 单一 Poller 场景（最常见）：拉完即可重置 Global，下一帧从 0 开始累积。
	 *   * 多 RS（PIE 多实例 / Server+Client 同进程）：必须等所有 Poller 都拉完才
	 *     能 Reset，否则会有 RS 漏掉脏。这里通过 ResetIfSinglePoller + 标记
	 *     bShouldResetPolledGlobalDirtyTracker，在 ReconcilePolledList 里二次尝试。
	 */
	void ApplyAndTryResetGlobalDirtyObjectList();

	/** Can only be accessed via FDirtyObjectsAccessor */
	// 中文：返回本帧脏 BitArrayView；线程安全检查保证只在 LockExternalAccess
	// 状态下被读（防止 Mark 期间被 Filter/Poll 读到不一致快照）。
	FNetBitArrayView GetDirtyNetObjectsThisFrame();

	/** Propagate properties marked dirty for given object and OwnerIndex */
	// 中文：把"对象的某些属性 RepIndex 脏"投射到对应 fragment 的 MemberPollMask，
	// 让 Polling 阶段只精确 Poll 这些属性。返回值表示该对象是否需要被整体标脏。
	bool MarkPushbasedPropertiesDirty(FInternalNetRefIndex ObjectIndex, uint16 OwnerIndex, const FNetBitArrayView& DirtyProperties);

private:

	// Dirty objects that persist across frames.
	// 中文：跨帧累积的脏对象集合。直到对象被 Poll 之前都保留——典型场景：
	// 对象在帧 N 被标脏但因为 PollFrequency 节流到帧 N+3 才轮询。
	FNetBitArray AccumulatedDirtyNetObjects;

    // Objects that want to force a replication this frame
	// 中文：强制本帧立刻 Poll & 复制（绕过 PollFrequency 限制），由
	// UReplicationSystem::ForceNetUpdate 设置。
	FNetBitArray ForceNetUpdateObjects;

	// List of objects set to be dirty this frame. Is always reset at the end of the net tick flush
	// 中文：纯本帧脏列表；ReconcilePolledList 末尾整体清零。
	FNetBitArray DirtyNetObjects;

	const FNetRefHandleManager* NetRefHandleManager = nullptr;
	
	// 中文：在 GlobalDirtyNetObjectTracker 中的 Poller 句柄；多 RS 共享 Global，
	// 各自持一个 Handle 决定何时能 Reset。
	FGlobalDirtyNetObjectTracker::FPollHandle GlobalDirtyTrackerPollHandle;

	uint32 ReplicationSystemId;

	// 中文：当前 BitArray 容量（位数）；用于 Mark 时的越界保护。
	uint32 NetObjectIdCount = 0;
	
	// 中文：见 ApplyAndTryResetGlobalDirtyObjectList 注释——多 Poller 场景的延迟 Reset 信号。
	bool bShouldResetPolledGlobalDirtyTracker = false;

#if UE_NET_THREAD_SAFETY_CHECK
	// 中文：线程安全断言开关。Lock/Allow 切换。
	bool bIsExternalAccessAllowed = false;
#endif

#if UE_NET_IRIS_CSV_STATS
	// 中文：CSV Profiler 计数（PushModel 触发的脏对象数 / Force 调用次数）。
	int32 PushModelDirtyObjectsCount = 0;
	int32 ForceNetUpdateObjectsCount = 0;
#endif
};

/**
 * Gives access to the list of dirty objects while detecting non-thread safe access to it.
 *
 * 中文：DirtyNetObjects RAII 访问器——构造时 LockExternalAccess（禁止其它代码
 * 同时 Mark），析构时 AllowExternalAccess。Filtering/Polling 等以只读姿态遍历。
 */
class FDirtyObjectsAccessor
{
public:
	FDirtyObjectsAccessor(FDirtyNetObjectTracker& InDirtyNetObjectTracker)
		: DirtyNetObjectTracker(InDirtyNetObjectTracker)
	{
		DirtyNetObjectTracker.LockExternalAccess();
	}

	~FDirtyObjectsAccessor()
	{
		DirtyNetObjectTracker.AllowExternalAccess();
	}

	FNetBitArrayView GetDirtyNetObjects()				{ return DirtyNetObjectTracker.GetDirtyNetObjectsThisFrame(); }
	const FNetBitArrayView GetDirtyNetObjects() const	{ return DirtyNetObjectTracker.GetDirtyNetObjectsThisFrame(); }

private:
	FDirtyNetObjectTracker& DirtyNetObjectTracker;
};

}

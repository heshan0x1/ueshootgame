// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================================
// DeltaCompressionBaselineManager.h（Private，Iris/ReplicationSystem/DeltaCompression 子模块入口）
// ---------------------------------------------------------------------------------------------
// 模块定位：Iris 的 DeltaCompression 调度中心。
//
// 一句话职责：
//   决定"何时为 (Object, Connection) 创建/复用/释放/作废基线"，并把上层的脏 ChangeMask 累积进
//   per-conn 累积位图，以便基线创建瞬间一次性"拍照"成不可变快照。
//
// 关键概念：
//
//   1. 每个对象至多 MaxBaselineCount=2 个基线槽（"双基线"机制）
//      ┌────────────────────────────────────────────────────────────┐
//      │ Object[i] × Connection[c]:                                 │
//      │   Baselines[0]   Baselines[1]                              │
//      │   ┌─────────┐    ┌─────────┐                               │
//      │   │ ChMask  │    │ ChMask  │                               │
//      │   │ StateInf│    │ StateInf│ → BaselineStorage 引用计数槽位 │
//      │   └─────────┘    └─────────┘                               │
//      │ 这两个槽位由 ReplicationWriter 状态机轮换使用：              │
//      │ - 一个是"上一次确认/发送的基线"                             │
//      │ - 另一个是"刚发出但还没拿到 ACK 的预备基线"                 │
//      └────────────────────────────────────────────────────────────┘
//
//   2. 每帧 PreSendUpdate 流程：
//      ┌────────────────────────────────────────────────────────────┐
//      │ ++FrameCounter                                             │
//      │ UpdateScope                ← 进/出 scope 的对象 add/remove  │
//      │ UpdateDirtyStateMasks      ← 把 ChangeMaskCache 中的脏位     │
//      │                              累加到 per-conn ChangeMasks    │
//      │ InvalidateBaselinesDueToModifiedConditionals               │
//      │   ← 消费 InvalidationTracker.Infos，逐 (Obj, Conn) 释放基线│
//      │ ConstructBaselineSharingContext ← 同帧基线共享去重         │
//      └────────────────────────────────────────────────────────────┘
//
//   3. 帧内 Writer 调用：
//      Writer.Write 期间会按 priority 调用 CreateBaseline(Conn,Obj,Idx)：
//      - 如果"本帧已有同对象基线"→ Reserve 复用（共享 StateBuffer）；
//      - 否则按节流策略（FramesSincePrevBaselineCreation ≥
//                       MinimumNumberOfFramesBetweenBaselines）决定是否真的建；
//      - 不允许建则返回 invalid baseline，Writer 回退到 full 序列化。
//      建立成功后：把 per-conn ChangeMask 拷进 InternalBaseline.ChangeMask 并清零，
//                  开始新一轮累积。
//
//   4. PostSendUpdate：
//      - DestructBaselineSharingContext：对本帧 Reserve 出来的所有基线调用
//        OptionallyCommitAndDoReleaseBaseline。还有引用 → 真克隆；没引用 → Cancel。
//
//   5. 失效路径与 BaselineState 状态机（per-conn 视角）：
//      Pending  ─Create─→  Active  ─Invalidated/Conn 退出/被新基线挤掉─→  释放
//        ↑                  │
//        │                  └─Conditionals/Filter/Owner 变化─→ Tracker 登记
//        └────────（下一帧 Writer 重新申请新基线）
//
//   6. 启用关：net.Iris.EnableDeltaCompression / net.Iris.MinimumNumberOfFramesBetweenBaselines。
//
//   7. 必要前提：对象的 Protocol 必须带 EReplicationProtocolTraits::SupportsDeltaCompression
//      （由 protocol 编译期决定，详见 ReplicationProtocol.{h,cpp}）。否则
//      SetDeltaCompressionStatus(Allow) 也会被强制降级为 Disallow。
// =============================================================================================

#pragma once

#include "Containers/Array.h"
#include "Net/Core/NetBitArray.h"
#include "Iris/ReplicationSystem/DeltaCompression/DeltaCompressionBaseline.h"
#include "Iris/ReplicationSystem/DeltaCompression/DeltaCompressionBaselineStorage.h"

class UReplicationSystem;

namespace UE::Net
{
	enum class ENetObjectDeltaCompressionStatus : unsigned;
}

namespace UE::Net::Private
{
	struct FChangeMaskCache;
	class FDeltaCompressionBaselineInvalidationTracker;
	typedef uint32 FInternalNetRefIndex;
	class FNetRefHandleManager;
	class FReplicationConnections;
}

namespace UE::Net::Private
{

/** Is the delta compression feature enabled? Togglable via net.Iris.EnableDeltaCompression. */
bool IsDeltaCompressionEnabled();

/** 初始化参数集合。由 FReplicationSystemImpl::Init 阶段填充并下发。 */
struct FDeltaCompressionBaselineManagerInitParams
{
	const FDeltaCompressionBaselineInvalidationTracker* BaselineInvalidationTracker = nullptr; // 失效事件队列消费源
	FReplicationConnections* Connections = nullptr;            // 枚举 ValidConnections / 取 MaxConnectionCount
	const FNetRefHandleManager* NetRefHandleManager = nullptr; // 查 Protocol / scope 位图
	FReplicationStateStorage* ReplicationStateStorage = nullptr; // 真正的 state buffer 分配器（传给 BaselineStorage）
	UReplicationSystem* ReplicationSystem = nullptr;           // 仅 NetTrace 需要
	FInternalNetRefIndex MaxNetObjectCount = 0;                // 全局对象数上限
	FInternalNetRefIndex MaxInternalNetRefIndex = 0;           // 内部 index 上限（同步给位图）
	uint32 MaxDeltaCompressedObjectCount = 0;                  // DC 启用对象数上限（取 min(它, MaxNetObjectCount)）
};

/** PreSendUpdate 入口参数。本帧脏位的来源：上层 QuantizeDirtyStateData 产生的 ChangeMaskCache。 */
struct FDeltaCompressionBaselineManagerPreSendUpdateParams
{
	const FChangeMaskCache* ChangeMaskCache = nullptr;
};

/** PostSendUpdate 入口参数（暂为空，预留扩展）。 */
struct FDeltaCompressionBaselineManagerPostSendUpdateParams
{
};

/**
 * DeltaCompression 子模块入口。
 *
 * 与各子组件协作：
 *   * FDeltaCompressionBaselineStorage              —— 真正的 buffer 池 + 引用计数 + 二段提交；
 *   * FDeltaCompressionBaselineInvalidationTracker  —— 单帧聚合失效事件；
 *   * FReplicationWriter                            —— SerializeDelta 路径调用 CreateBaseline / GetBaseline / DestroyBaseline / LostBaseline；
 *   * FNetRefHandleManager                          —— 提供当前/上一帧 scope 位图；
 *   * FChangeMaskCache                              —— 提供本帧脏对象 + 脏 ChangeMask 字流。
 */
class FDeltaCompressionBaselineManager
{
public:
	enum : uint32 { MaxBaselineCount = 2 };       // 每 (Obj, Conn) 同时最多 2 个基线槽（双基线机制）
	enum : uint32 { InvalidBaselineIndex = MaxBaselineCount }; // 哨兵值
	enum : uint32 { BaselineIndexBitCount = 2 };  // 序列化所需位数（2 bit 足够表达 0/1/Invalid）

	FDeltaCompressionBaselineManager();
	~FDeltaCompressionBaselineManager();

	void Init(FDeltaCompressionBaselineManagerInitParams& InitParams);
	void Deinit();

	/** Called when the maximum InternalNetRefIndex increased and we need to realloc our lists */
	// 中文：扩容内部 DeltaCompressionEnabledObjects 位图与 ObjectIndexToObjectInfoIndex 表。
	void OnMaxInternalNetRefIndexIncreased(UE::Net::Private::FInternalNetRefIndex NewMaxInternalIndex);

	/**
	 * 帧前更新（在 Writer.Write 之前）。流程：
	 *   ++FrameCounter
	 *   → UpdateScope                            （对象进/出 scope）
	 *   → UpdateDirtyStateMasks(ChangeMaskCache) （per-conn 累计脏位）
	 *   → InvalidateBaselinesDueToModifiedConditionals（消费 Tracker 失效事件）
	 *   → ConstructBaselineSharingContext       （准备同帧共享去重）
	 */
	void PreSendUpdate(FDeltaCompressionBaselineManagerPreSendUpdateParams& UpdateParams);

	/**
	 * 帧后清理（在 Writer.Write 之后、PostSendUpdate 阶段）：
	 *   DestructBaselineSharingContext —— 对本帧 Reserve 出来的所有槽位调用
	 *   OptionallyCommitAndDoReleaseBaseline。仍有引用→真克隆；归零→Cancel。
	 */
	void PostSendUpdate(FDeltaCompressionBaselineManagerPostSendUpdateParams& UpdateParams);

	/** 新连接接入：当前为空，但保留接口便于后续扩展（per-conn 状态机已经按 MaxConnectionCount 一次性预分配）。 */
	void AddConnection(uint32 ConnectionId);
	/** 连接断开：释放该连接所有 InternalBaseline，并按 baseline count 清理 PerObjectInfo（若不再需要）。 */
	void RemoveConnection(uint32 ConnectionId);

	/**
	 * 设置某对象的 DC 状态。说明：
	 *   * 必须协议本身 SupportsDeltaCompression；
	 *   * 必须全局 net.Iris.EnableDeltaCompression 为 true；
	 * 否则即便传 Allow 也会被强制为 Disallow。
	 */
	void SetDeltaCompressionStatus(FInternalNetRefIndex Index, ENetObjectDeltaCompressionStatus Status);
	ENetObjectDeltaCompressionStatus GetDeltaCompressionStatus(FInternalNetRefIndex Index) const;

	uint32 GetMaxDeltaCompressedObjectCount() const;

	/**
	 * Creates a baseline if the policy allows it. May return an invalid baseline. The only guarantee is it will be
	 * valid for the current SendUpdate(). Both events outside of this manager's control and baseline policies, such
	 * as how many may exist in total and per object and how frequent state changes occur, may invalidate the baseline
	 * in the future. If unlucky a state changing conditional is enabled which forces the baseline to be invalidated
	 * as early as the next frame.
	 *
	 * ----------------------------------------------------------------------------
	 * 中文：尝试为 (Conn, Obj, BaselineIdx) 创建一个基线。可能因下列原因返回无效：
	 *   * 对象未启用 DC / 未在 scope；
	 *   * 节流策略：距上次创建不足 MinimumNumberOfFramesBetweenBaselines 帧；
	 *   * 池满（BaselineStorage 没有空闲槽位）。
	 * 同帧内多连接同对象的 CreateBaseline 会通过 BaselineSharingContext 共享同一槽位，
	 * 仅在第一次时真正 Reserve，后续 AddRef。
	 * ----------------------------------------------------------------------------
	 */
	FDeltaCompressionBaseline CreateBaseline(uint32 ConnId, uint32 ObjectIndex, uint32 BaselineIndex);

	/** Destroys a baseline if it's valid. */
	// 中文：销毁基线（refcount-1）。已无效的索引可以被安全调用，作 no-op。
	void DestroyBaseline(uint32 ConnId, uint32 ObjectIndex, uint32 BaselineIndex);
	/**
	 * Destroys a baseline if it's valid but merges the changemask into the connection specific one for this object
	 * so that a future call to GetBaseline() will include at least that changemask.
	 *
	 * ----------------------------------------------------------------------------
	 * 中文：DestroyBaseline 的"丢包失败"变体。当 Writer 因 NACK / 包丢失要丢弃某基线时，
	 *       把基线累计的 ChangeMask 合并回该 Conn 的当前累计位图——这样下次能补发完整 diff。
	 * ----------------------------------------------------------------------------
	 */
	void LostBaseline(uint32 ConnId, uint32 ObjectIndex, uint32 BaselineIndex);

	/**
	 * Returns a valid baseline if it exists, an invalid one if it no longer exists. The manager is free to invalidate
	 * baselines at its own discretion.
	 *
	 * ----------------------------------------------------------------------------
	 * 中文：Writer 在 SerializeDelta 时调用以拿到上次的基线（StateBuffer + ChangeMask）。
	 * 注意：Manager 可能在两次调用间作废基线，调用方必须用返回值的 IsValid() 判断。
	 * ----------------------------------------------------------------------------
	 */
	FDeltaCompressionBaseline GetBaseline(uint32 ConnId, uint32 ObjectIndex, uint32 BaselineIndex) const;

	[[nodiscard]] FString PrintDeltaCompressionStatus(uint32 ConnectionId, FInternalNetRefIndex ObjectIndex) const;

private:
	using ObjectInfoIndexType = uint16; // 内部 PerObjectInfo 槽位号；与 InternalNetRefIndex 通过 ObjectIndexToObjectInfoIndex 一一映射

	enum : unsigned
	{
		InvalidObjectInfoIndex = 0,
		InvalidBaselineStateInfoIndex = 0,

		// Must match NetRefHandleManager::InvalidInternalIndex
		InvalidInternalIndex = 0,

		ObjectInfoGrowCount = 128, // ObjectInfoStorage 一次扩容 128 个槽
	};

	/** DestroyBaseline 的子语义选项：直接丢弃 ChangeMask vs 把它 OR 回 per-conn 累计。 */
	enum class EChangeMaskBehavior : unsigned
	{
		Discard,
		Merge,
	};

	/**
	 * 单个基线槽（双基线之一）。指向 BaselineStorage 中的真实数据 + 自己的 ChangeMask。
	 *
	 * 状态机（每对象、每连接、每槽）：
	 *
	 *   Empty                         ┐
	 *     ↓ CreateBaseline             │
	 *   Reserved (this frame)          │
	 *     ↓ PostSendUpdate (refcount>0)│
	 *   Active (committed)             │
	 *     ↓ Lost/Destroy/Invalidate   ←┘
	 *   Empty
	 *
	 * BaselineStateInfoIndex == InvalidBaselineStateInfoIndex 视为 Empty。
	 */
	class FInternalBaseline
	{
	public:
		bool IsValid() const { return BaselineStateInfoIndex != InvalidBaselineStateInfoIndex; }

		ChangeMaskStorageType* ChangeMask = nullptr;        // 自基线建立以来累积的脏位
		uint32 BaselineStateInfoIndex = InvalidBaselineStateInfoIndex; // 指向 BaselineStorage 槽位号
	};

	/** 一对双基线（每 (Object, Conn) 一份）。 */
	class FObjectBaselineInfo
	{
	public:
		FInternalBaseline Baselines[2];
	};

	/**
	 * 每对象信息（变长结构！末尾 BaselinesForConnections[1] 实际尺寸为 MaxConnectionCount）。
	 *
	 * 内存布局（一段连续字节）：
	 *   ┌─FPerObjectInfo header（含 BaselinesForConnections[0]）─┐
	 *   ├─BaselinesForConnections[1] ───────────────────────────┤
	 *   ├─...                                                   │
	 *   └─BaselinesForConnections[MaxConnectionCount-1] ────────┘
	 *
	 * 由 BytesPerObjectInfo 计算（在 Init 中）；通过 ObjectInfoStorage（uint8 数组）按下标访问。
	 *
	 * ChangeMask 存储与之分离，统一在 ChangeMasksForConnections 单一指针内：
	 *   ChangeMasksForConnections 布局（按 ChangeMaskStride 字 × 3 × MaxConnectionCount）：
	 *     [Conn0_PerConnAccum] ... [Conn(N-1)_PerConnAccum]
	 *     [Conn0_Baseline0]    [Conn0_Baseline1]
	 *     [Conn1_Baseline0]    [Conn1_Baseline1]
	 *     ...
	 *   见 GetChangeMaskPointerForConnection / GetChangeMaskPointerForBaseline。
	 */
	class FPerObjectInfo
	{
	private:
		// Use ConstructPerObjectInfo instead.
		FPerObjectInfo() = delete;
		// Use DestructPerObjectInfo instead.
		~FPerObjectInfo() = delete;

	public:
		// Single pointer for changemasks for all connections.
		// 中文：所有连接的 ChangeMask（Per-Conn 累计 + 双基线 ChangeMask）共享一段连续 buffer。
		ChangeMaskStorageType* ChangeMasksForConnections;

		// ObjectIndex
		uint32 ObjectIndex;

		// Local frame number of when this object last had a baseline created. 
		// 中文：节流计数。CreateBaseline 时更新；判断 IsAllowedToCreateBaselineForObject。
		uint32 PrevBaselineCreationFrame;

		// How many ChangeMaskStorageTypes per change mask.
		uint16 ChangeMaskStride;

		// Baselines for all connections. Array has same size as MaxConnectionCount. This member needs to be last!
		// 中文：变长尾巴！声明里写 [1] 但实际按 MaxConnectionCount 分配。
		FObjectBaselineInfo BaselinesForConnections[1];

		// No members after BaselinesForConnections!
	};

	/**
	 * 同帧基线共享上下文。
	 * 用途：避免同一对象在同一帧被多个连接重复 Reserve（节省 BaselineStorage 槽位）。
	 *
	 * 工作方式：
	 *   * CreateBaseline 第一次为某 ObjectInfoIndex Reserve 时，把 StateInfoIndex 记到
	 *     ObjectInfoIndexToBaselineInfoIndex[InfoIdx] 并 SetBit；
	 *   * 同帧后续连接 CreateBaseline 时检测 SetBit，直接 GetBaselineReservationForCurrentState
	 *     拿同一槽位（共享 StateBuffer），AddRef 即可。
	 *   * PostSendUpdate 阶段统一 OptionallyCommitAndDoReleaseBaseline。
	 */
	struct FBaselineSharingContext
	{
		FNetBitArray ObjectInfoIndicesWithNewBaseline;
		TArray<DeltaCompressionBaselineStateInfoIndexType> ObjectInfoIndexToBaselineInfoIndex;
		uint32 CreatedBaselineCount = 0;
	};

private:

	/** PreSendUpdate 中：根据 NetRefHandleManager 的 scope 位图差分，给进/出 scope 的对象 add/remove PerObjectInfo。 */
	void UpdateScope();
	/** PreSendUpdate 中：把 ChangeMaskCache 中的本帧脏位 OR 进每个 ValidConnection 的 per-conn 累计 ChangeMask。 */
	void UpdateDirtyStateMasks(const FChangeMaskCache* ChangeMaskCache);

	/** 对象进 scope：分配 PerObjectInfo + ChangeMasksForConnections（按 Protocol 的 ChangeMaskBitCount × 3 × MaxConn 字）。 */
	void AddObjectToScope(uint32 ObjectIndex);
	/** 对象出 scope：若没有活跃基线则立即 free PerObjectInfo；否则等基线全部释放后再回收。 */
	void RemoveObjectFromScope(uint32 ObjectIndex);

	FPerObjectInfo* AllocPerObjectInfoForObject(uint32 ObjectIndex);
	void FreePerObjectInfoForObject(uint32 ObjectIndex);
	
	void ConstructPerObjectInfo(FPerObjectInfo*) const;
	void DestructPerObjectInfo(FPerObjectInfo*);

	ObjectInfoIndexType AllocPerObjectInfo();
	void FreePerObjectInfo(ObjectInfoIndexType Index);

	FPerObjectInfo* GetPerObjectInfo(ObjectInfoIndexType Index);
	const FPerObjectInfo* GetPerObjectInfo(ObjectInfoIndexType Index) const;

	FPerObjectInfo* GetPerObjectInfoForObject(uint32 ObjectIndex);
	const FPerObjectInfo* GetPerObjectInfoForObject(uint32 ObjectIndex) const;

	void FreeAllPerObjectInfos();

	/** Destroy/Lost 共用实现：按 EChangeMaskBehavior 决定是否把 baseline ChangeMask 合并回 per-conn。 */
	void DestroyBaseline(uint32 ConnId, uint32 ObjectIndex, uint32 BaselineIndex, EChangeMaskBehavior ChangeMaskBehavior);

	/** 释放单个 InternalBaseline：调用 BaselineStorage.ReleaseBaseline + 清 BaselineStateInfoIndex。 */
	void ReleaseInternalBaseline(FInternalBaseline& InternalBaseline);

	/**
	 * 调整 BaselineCounts[ObjectInfoIndex]。当计数归零且对象已不在 DC 启用集合中时，
	 * 立即释放 PerObjectInfo（避免内存膨胀）。
	 */
	void AdjustBaselineCount(const FPerObjectInfo* ObjectInfo, ObjectInfoIndexType ObjectInfoIndex, int16 Adjustment);

	/** 连接断开时遍历所有 PerObjectInfo 释放该连接的两个基线槽。 */
	void ReleaseBaselinesForConnection(uint32 ConnId);

	/** 同帧基线共享上下文的开/关。 */
	void ConstructBaselineSharingContext(FBaselineSharingContext&);
	void DestructBaselineSharingContext(FBaselineSharingContext&);

	/** 检查 Protocol 是否带 SupportsDeltaCompression trait。 */
	bool DoesObjectSupportDeltaCompression(uint32 ObjectIndex) const;

	/** ChangeMask 子区域的指针计算：见 FPerObjectInfo 注释中的内存布局图。 */
	ChangeMaskStorageType* GetChangeMaskPointerForBaseline(const FPerObjectInfo*, uint32 ConnId, uint32 BaselineIndex) const;
	ChangeMaskStorageType* GetChangeMaskPointerForConnection(const FPerObjectInfo*, uint32 ConnId) const;

	/**
	  * Whether a new baseline may be created for this object this frame based on baseline creation throttling policies.
	  * It's assumed that the object supports delta compression.
	  *
	  * ----------------------------------------------------------------------------
	  * 中文：节流策略——
	  *   * 当前 BaselineCount==0 时无条件允许（首次或全部失效后必须建一次）；
	  *   * 否则要求距 PrevBaselineCreationFrame 的帧数 ≥ MinimumNumberOfFramesBetweenBaselines。
	  *   * MinimumNumberOfFramesBetweenBaselines<0 视为永远禁止后续基线（仅用一次基线，比对到底）。
	  * ----------------------------------------------------------------------------
	  */
	bool IsAllowedToCreateBaselineForObject(uint32 ConnId, uint32 ObjectIndex, const FPerObjectInfo*, ObjectInfoIndexType InfoIndex) const;

	/** PreSendUpdate 内调用：消费 InvalidationTracker，逐 (Object, Conn) 释放对应 InternalBaseline。 */
	void InvalidateBaselinesDueToModifiedConditionals();

private:
	/** 子组件：基线槽位池 + 引用计数 + Reserve/Commit 二段提交。 */
	FDeltaCompressionBaselineStorage BaselineStorage;

	/** "对象当前是否启用 DC" 位图。下标 = InternalNetRefIndex。SetDeltaCompressionStatus / UpdateScope 维护。 */
	FNetBitArray DeltaCompressionEnabledObjects;
	/** PerObjectInfo 槽位"已使用"位图。下标 = ObjectInfoIndex。 */
	FNetBitArray UsedPerObjectInfos;
	/** InternalNetRefIndex → ObjectInfoIndex 映射；0 表示该对象未分配 PerObjectInfo。 */
	TArray<ObjectInfoIndexType> ObjectIndexToObjectInfoIndex;
	/** 变长 PerObjectInfo 的连续字节存储；按 BytesPerObjectInfo × 槽数索引。 */
	TArray<uint8, TAlignedHeapAllocator<alignof(FPerObjectInfo)>> ObjectInfoStorage;
	// This array will only be large enough to hold baseline counts for MaxDeltaCompressedObjectCount baselines.
	// Indexed using ObjectInfoIndex.
	/**
	 * 每对象当前活跃基线数（跨所有连接、所有槽位）。
	 * 用途：判断"出 scope 时能否立即释放 PerObjectInfo"——非零则等到全部释放后再回收。
	 */
	TArray<uint16> BaselineCounts;

	/** 给所有 ChangeMask 段使用的全局分配器（chunk-pool 风格，节省单对象 alloc 次数）。 */
	FGlobalChangeMaskAllocator ChangeMaskAllocator;

	FReplicationConnections* Connections = nullptr;
	const FNetRefHandleManager* NetRefHandleManager = nullptr;
	UReplicationSystem* ReplicationSystem = nullptr;
	const FDeltaCompressionBaselineInvalidationTracker* BaselineInvalidationTracker = nullptr;
	uint32 MaxConnectionCount = 0;
	uint32 BytesPerObjectInfo = 0;          // sizeof(header) + (MaxConn-1)*sizeof(FObjectBaselineInfo)，对齐到 alignof(FPerObjectInfo)
	uint32 MaxDeltaCompressedObjectCount = 0;
	uint32 FrameCounter = 0;                // 节流计数基准；PreSendUpdate 每次 +1

	// Only used during send update to prevent creating multiple baselines for the same object.
	FBaselineSharingContext BaselineSharingContext;
};


inline uint32 FDeltaCompressionBaselineManager::GetMaxDeltaCompressedObjectCount() const
{
	return MaxDeltaCompressedObjectCount;
}

inline FDeltaCompressionBaselineManager::FPerObjectInfo* FDeltaCompressionBaselineManager::GetPerObjectInfo(ObjectInfoIndexType Index)
{
	const SIZE_T ObjectInfoStorageIndex = Index*BytesPerObjectInfo;

	checkSlow(ObjectInfoStorageIndex < (uint32)ObjectInfoStorage.Num());

	uint8* StoragePointer = ObjectInfoStorage.GetData() + ObjectInfoStorageIndex;
	return reinterpret_cast<FPerObjectInfo*>(StoragePointer);
}

inline const FDeltaCompressionBaselineManager::FPerObjectInfo* FDeltaCompressionBaselineManager::GetPerObjectInfo(ObjectInfoIndexType Index) const
{
	const SIZE_T ObjectInfoStorageIndex = Index*BytesPerObjectInfo;

	checkSlow(ObjectInfoStorageIndex < (uint32)ObjectInfoStorage.Num());

	const uint8* StoragePointer = ObjectInfoStorage.GetData() + ObjectInfoStorageIndex;
	return reinterpret_cast<const FPerObjectInfo*>(StoragePointer);
}

inline FDeltaCompressionBaselineManager::FPerObjectInfo* FDeltaCompressionBaselineManager::GetPerObjectInfoForObject(uint32 ObjectIndex)
{
	checkSlow(ObjectIndex != InvalidInternalIndex);
	checkSlow(ObjectIndex < (uint32)ObjectIndexToObjectInfoIndex.Num());

	const ObjectInfoIndexType InfoIndex = ObjectIndexToObjectInfoIndex[ObjectIndex];
	if (InfoIndex != InvalidObjectInfoIndex)
	{
		return GetPerObjectInfo(InfoIndex);
	}

	return nullptr;
}

inline const FDeltaCompressionBaselineManager::FPerObjectInfo* FDeltaCompressionBaselineManager::GetPerObjectInfoForObject(uint32 ObjectIndex) const
{
	checkSlow(ObjectIndex != InvalidInternalIndex);
	checkSlow(ObjectIndex < (uint32)ObjectIndexToObjectInfoIndex.Num());

	const ObjectInfoIndexType InfoIndex = ObjectIndexToObjectInfoIndex[ObjectIndex];
	if (InfoIndex != InvalidObjectInfoIndex)
	{
		return GetPerObjectInfo(InfoIndex);
	}

	return nullptr;
}

inline ChangeMaskStorageType* FDeltaCompressionBaselineManager::GetChangeMaskPointerForBaseline(const FPerObjectInfo* ObjectInfo, uint32 ConnId, uint32 BaselineIndex) const
{
	// Layout of storage per object is:
	// ChangeMaskForConnections[0 .. ConnectionCount]
	// ChangeMaskForBaselines[Conn0[0+1] .. ConnConnectionCount[0+1]]
	//
	// 中文：偏移公式  base + (MaxConn + 2*Conn + Baseline) * Stride
	//      解释：前 MaxConn 段是 per-conn 累计 ChangeMask，之后是 Conn × 2 个 baseline ChangeMask 段。
	const uint32 ChangeMaskStride = ObjectInfo->ChangeMaskStride;
	const SIZE_T BaselineChangeMaskOffset = (MaxConnectionCount + 2U*ConnId + BaselineIndex)*ChangeMaskStride;
	ChangeMaskStorageType* ChangeMaskPointer = ObjectInfo->ChangeMasksForConnections + BaselineChangeMaskOffset;
	return ChangeMaskPointer; 
}

inline ChangeMaskStorageType* FDeltaCompressionBaselineManager::GetChangeMaskPointerForConnection(const FPerObjectInfo* ObjectInfo, uint32 ConnId) const
{
	// 中文：per-conn 累计段在 buffer 起始处，按 ConnId × Stride 寻址。
	const uint32 ChangeMaskStride = ObjectInfo->ChangeMaskStride;
	const SIZE_T ConnectionChangeMaskOffset = ConnId*ChangeMaskStride;
	ChangeMaskStorageType* ChangeMaskPointer = ObjectInfo->ChangeMasksForConnections + ConnectionChangeMaskOffset;
	return ChangeMaskPointer; 
}

}

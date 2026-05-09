// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================================
// DeltaCompressionBaselineInvalidationTracker.h（Private）
// ---------------------------------------------------------------------------------------------
// 模块定位：Iris / ReplicationSystem / DeltaCompression 子模块的"基线作废收集器"。
//
// 一句话职责：
//   把一帧内来自任意源头的"该 (Object, ConnId) 的基线已不可信"事件累积成一个 list，
//   交给 FDeltaCompressionBaselineManager 在它的 PreSendUpdate 阶段统一处理（释放 InternalBaseline）。
//
// 触发场景（来自 ReplicationSystem.md §6.4 + 代码调用方）：
//   * SetReplicationCondition / SetReplicationConditionConnectionFilter 改变了对象级条件
//     → FReplicationConditionals::InvalidateBaselinesForObjectHierarchy
//   * SetPropertyCustomCondition(true)：开启 custom condition 引入新字段
//     → FReplicationConditionals::SetPropertyCustomCondition
//   * SetPropertyDynamicCondition：动态条件由"可能未发"变为"将要发"
//     → DynamicConditionChangeRequiresBaselineInvalidation
//   * SetOwningConnection 改变 owner（影响 OwnerOnly 等条件）
//   * Filter 子系统 / Scope 改变（在 Manager.UpdateScope 里会自动清理）
//
// 与 NetRefHandleManager 协作：
//   * InvalidatedObjects 位图按 InternalNetRefIndex 索引；
//   * BaselineManager 通过 ObjectIndexToObjectInfoIndex 查找内部 PerObjectInfo，
//     再按 (ObjectInfoIndex, ConnId) 释放 InternalBaseline。
//
// 帧阶段：
//   PreSendUpdate                 ← 收集（在 Conditionals 处理前后均会发生）
//   Manager.PreSendUpdate
//     └→ InvalidateBaselinesDueToModifiedConditionals 消费 InvalidationInfos
//   PostSendUpdate                ← 清空 list 与位图
// =============================================================================================

#pragma once

#include "CoreTypes.h"
#include "Containers/Array.h"
#include "Containers/ArrayView.h"
#include "Net/Core/NetBitArray.h"
#include "Iris/ReplicationSystem/DeltaCompression/DeltaCompressionBaseline.h"
#include "Iris/ReplicationSystem/DeltaCompression/DeltaCompressionBaselineStorage.h"

namespace UE::Net::Private
{
	class FDeltaCompressionBaselineManager;
	typedef uint32 FInternalNetRefIndex;
}

namespace UE::Net::Private
{

struct FDeltaCompressionBaselineInvalidationTrackerInitParams
{
	const FDeltaCompressionBaselineManager* BaselineManager = nullptr; // 反查 ENetObjectDeltaCompressionStatus
	FInternalNetRefIndex MaxInternalNetRefIndex = 0;                   // InvalidatedObjects 位图大小
};

/**
 * 基线作废收集器。无线程安全，必须在 ReplicationSystem 单线程编排路径中访问。
 */
class FDeltaCompressionBaselineInvalidationTracker
{
public:
	enum Constants : uint32
	{
		/**
		 * "对所有连接作废"的特殊 ConnId 值（=0）。Manager 在消费时按此特判。
		 * 选 0 是因为正常 ConnectionId 从 1 起，0 不会与真实连接冲突。
		 */
		InvalidateBaselineForAllConnections = 0U,
	};

	/** 单条作废记录：被作废的对象 + 连接（可能是 InvalidateBaselineForAllConnections）。 */
	struct FInvalidationInfo
	{
		uint32 ConnId = InvalidateBaselineForAllConnections;
		FInternalNetRefIndex ObjectIndex = 0U;
	};

public:
	FDeltaCompressionBaselineInvalidationTracker();
	~FDeltaCompressionBaselineInvalidationTracker();

	void Init(FDeltaCompressionBaselineInvalidationTrackerInitParams& InitParams);

	// It is up to calling code to also do this for eventual subobjects.
	/**
	 * 中文：登记一次作废事件。注意：本类不会自动遍历 SubObject——
	 *       是否对 root + 所有 owned-subobject 作废由调用方决定（Conditionals 走的就是这条路径）。
	 *
	 * 重复登记（相同 ObjectIndex + InvalidateBaselineForAllConnections）会被位图去重，
	 * 但同一对象的不同 ConnId 仍会逐条入 list。
	 */
	void InvalidateBaselines(FInternalNetRefIndex ObjectIndex, uint32 ConnId);

	// Returns an array of objects with enabled conditions such that DC baselines need to be invalidated.
	/** 中文：把本帧累积的 InvalidationInfos 暴露给 BaselineManager 消费（只读视图，不夺所有权）。 */
	TArrayView<const FInvalidationInfo> GetBaselineInvalidationInfos() const;

	/** 中文：当前实现仅占位，可在此插入对 ObjectIndex 的排序以提高 cache 命中率。 */
	void PreSendUpdate();
	/** 中文：本帧结束清空 list + 位图（下一帧重新累计）。 */
	void PostSendUpdate();

	/** Called when the maximum InternalNetRefIndex increased and we need to realloc our lists */
	void OnMaxInternalNetRefIndexIncreased(FInternalNetRefIndex NewMaxInternalIndex);

private:
	enum : unsigned
	{
		/** TArray 扩容步长，避免单条添加触发 realloc 抖动。 */
		InvalidationInfoGrowCount = 256,
	};

	/** 本帧的作废事件列表（按入队顺序，可能含重复；Manager 端逐条释放即可，重复不会出错）。 */
	TArray<FInvalidationInfo> InvalidationInfos;
	/**
	 * "对该对象本帧已登记过 InvalidateBaselineForAllConnections 一次" 的去重位图。
	 * 仅对全连接作废做去重（按连接的作废由 ConnId 区分，理论上量很小）。
	 */
	FNetBitArray InvalidatedObjects;

	const FDeltaCompressionBaselineManager* BaselineManager = nullptr;
};

inline TArrayView<const FDeltaCompressionBaselineInvalidationTracker::FInvalidationInfo> FDeltaCompressionBaselineInvalidationTracker::GetBaselineInvalidationInfos() const
{
	return MakeArrayView(InvalidationInfos);
}

}

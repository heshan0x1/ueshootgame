// Copyright Epic Games, Inc. All Rights Reserved.

// =====================================================================================================================
// ObjectScopeHysteresisUpdater.h —— "对象 Scope 滞后"机制的 per-connection 更新器。
// ---------------------------------------------------------------------------------------------------------------------
// 动机：见 ReplicationFilteringConfig.h。被动态过滤掉的对象在 N 帧内仍保持 in-scope，避免边缘抖动。
//
// 设计：
//   - 每个对象映射到一个紧凑 LocalIndex（0..MaxLocalIndex）；FrameCounters[LocalIndex] 持有剩余帧数；
//   - SetHysteresisFrameCount(NetRefIndex, N) 启动一次"滞后期"：自动分配 LocalIndex 并写入 N；
//   - Update(FramesSinceLastUpdate, OutObjectsToFilterOut) 每帧（或按节流间隔）减计数；
//     当 counter 已经"减到 < 0"（用 16-bit 算术 wrap，与 65536-FramesSinceLastUpdate 比较检测下溢）时把
//     LocalIndex 释放并把对应 InternalNetRefIndex 写入 OutObjectsToFilterOut（"该对象正式出 scope"）。
//   - RemoveHysteresis 在对象销毁/不需滞后时显式从更新器移除（O(1)）。
//
// 数据结构亮点：
//   - LocalIndex 池由 UsedLocalIndices 位图管理；FrameCounters / LocalIndexToNetRefIndex 同步增长。
//   - ObjectsToUpdate（按 InternalNetRefIndex 索引）位图供调用方做批查询。
//   - Update 内部以 4 个 counter 为一组手动展开循环、并以 word 级别跳过纯 0 段（典型场景下大部分对象不在更新中）。
// =====================================================================================================================

#pragma once

#include "CoreTypes.h"
#include "Containers/Array.h"
#include "Containers/Map.h"
#include "Net/Core/NetBitArray.h"

namespace UE::Net::Private
{
	typedef uint32 FInternalNetRefIndex;
}

namespace UE::Net::Private
{

class FObjectScopeHysteresisUpdater
{
public:
	/** 初始化：给 ObjectsToUpdate 位图分配 MaxObjectCount 位。 */
	void Init(uint32 MaxObjectCount);
	/** 释放所有内部容器。 */
	void Deinit();

	/** Called when the maximum InternalNetRefIndex increased and we need to realloc our lists */
	/** 索引上限扩容：扩 ObjectsToUpdate 位图。 */
	void OnMaxInternalNetRefIndexIncreased(FInternalNetRefIndex NewMaxInternalIndex);

	/** Sets an hysteresis frame count such that an object will be kept in scope until such many frames has passed. */
	/** 启动/重设对象的滞后期：分配 LocalIndex（若无）并写入剩余帧数。 */
	void SetHysteresisFrameCount(FInternalNetRefIndex NetRefIndex, uint16 HysteresisFrameCount);

	/** Remove an object from hysteresis update. Needed when an object goes out of scope. An object is automatically removed from hysteresis updates when a previously set frame count has expired. */
	/** 显式从更新器移除（O(1)）。计数到期后 Update() 内部也会自动移除。 */
	void RemoveHysteresis(FInternalNetRefIndex NetRefIndex);

	/** Removes all objects in the bitarray from hysteresis. */
	/** 批量按位图移除（与 ObjectsToUpdate 取交集逐个处理）。 */
	void RemoveHysteresis(const FNetBitArrayView& ObjectsToRemove);

	/** Removes all objects in the array from hysteresis. */
	/** 批量按数组移除。 */
	void RemoveHysteresis(TArrayView<const uint32> ObjectsToRemove);

	/** Adjusts the connection relevant objects based on hysteresis frame counts. */
	/**
	 * 推进帧数：把 FrameCounters 都减去 FramesSinceLastUpdate；过期对象写入 OutObjectsToFilterOut。
	 * FramesSinceLastUpdate ∈ (0, 128]（与 ReplicationFilteringConfig::HysteresisUpdateConnectionThrottling 上限一致）。
	 */
	void Update(uint8 FramesSinceLastUpdate, TArray<FInternalNetRefIndex>& OutObjectsToFilterOut);

	/** Whether any objects are updated for hysteresis. If not there's no point in calling Update(). */
	/** 短路：当前没有任何对象在滞后期，调用方可跳过整个 Update 流程。 */
	bool HasObjectsToUpdate() const;

	/** Returns the bitarray of objects affected by hysteresis */
	/** 当前在滞后期的对象位图视图（按 InternalNetRefIndex 索引）。 */
	FNetBitArrayView GetUpdatedObjects() const;

	/** Returns true if the object is currently updated. */
	bool IsObjectUpdated(FInternalNetRefIndex NetRefIndex) const;

private:
	enum : unsigned
	{
		LocalIndexGrowCount = 256U,   // 每次扩容 LocalIndex 池的步长（位/槽）。
	};

	typedef uint32 FLocalIndex;

	/** 找到（或新建）NetRefIndex 对应的 LocalIndex；必要时按 LocalIndexGrowCount 扩容三个对齐数组。 */
	FLocalIndex GetOrCreateLocalIndex(FInternalNetRefIndex NetRefIndex);
	/** 释放 LocalIndex：清位图、清 NetRefIndexToLocalIndex 反查项、清 ObjectsToUpdate 位。 */
	void FreeLocalIndex(FLocalIndex LocalIndex);

	// Per LocalIndex how many frames left to update before filtering out the object.
	/** 每个 LocalIndex 的剩余滞后帧数。Update 用 16-bit wrap 减算检测下溢。 */
	TArray<uint16> FrameCounters;
	// Lookup table for local index to InternalNetRefIndex.
	/** LocalIndex → InternalNetRefIndex（与 FrameCounters / UsedLocalIndices 同步增长）。 */
	TArray<FInternalNetRefIndex> LocalIndexToNetRefIndex;
	// Lookup map for InternalNetRefIndex to LocalIndex to be able to figure out whether an object already is assigned a LocalIndex or not.
	/** InternalNetRefIndex → LocalIndex（避免线性扫描）。 */
	TMap<FInternalNetRefIndex, FLocalIndex> NetRefIndexToLocalIndex;
	// A set bit indicates that the LocalIndex is used. Only stores MaxLocalIndex bits. The bitarray grows as needed.
	/** LocalIndex 占用位图（找空闲槽用 FindFirstZero）。 */
	FNetBitArray UsedLocalIndices;
	// A set bit indicates that the InternalNetRefIndex is being updated. Can always hold the MaxObjectCount passed to Init().
	/** InternalNetRefIndex 维度的"在更新中"位图。 */
	FNetBitArray ObjectsToUpdate;
};

inline bool FObjectScopeHysteresisUpdater::HasObjectsToUpdate() const
{
	return !NetRefIndexToLocalIndex.IsEmpty();
}

inline FNetBitArrayView FObjectScopeHysteresisUpdater::GetUpdatedObjects() const
{
	return MakeNetBitArrayView(ObjectsToUpdate);
}

inline bool FObjectScopeHysteresisUpdater::IsObjectUpdated(FInternalNetRefIndex ObjectIndex) const
{
	return ObjectsToUpdate.GetBit(ObjectIndex);
}

}

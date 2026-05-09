// Copyright Epic Games, Inc. All Rights Reserved.

// =====================================================================================================================
// ObjectScopeHysteresisUpdater.cpp —— FObjectScopeHysteresisUpdater 实现。
// 算法概述见 .h 头部说明。本 .cpp 关注：
//   - LocalIndex 池管理：基于位图 FindFirstZero + 按 LocalIndexGrowCount 扩容；
//   - Update() 中以 32-bit Word 为单位扫位图（跳过空段），再以 4 个 counter 为一个 nibble（4-bit 分支）展开；
//   - 用 16-bit 整数下溢检测过期：FilterOutCompareValue = 65536 - FramesSinceLastUpdate；减后 ≥ 该值意味"已下溢"。
// =====================================================================================================================

#include "Iris/ReplicationSystem/Filtering/ObjectScopeHysteresisUpdater.h"
#include "Iris/Core/IrisProfiler.h"
#include "Containers/ArrayView.h"

namespace UE::Net::Private
{

void FObjectScopeHysteresisUpdater::Init(uint32 MaxObjectCount)
{
	ObjectsToUpdate.Init(MaxObjectCount);
}

void FObjectScopeHysteresisUpdater::Deinit()
{
	FrameCounters.Empty();
	LocalIndexToNetRefIndex.Empty();
	NetRefIndexToLocalIndex.Empty();
	UsedLocalIndices.Empty();
	ObjectsToUpdate.Empty();
}

void FObjectScopeHysteresisUpdater::OnMaxInternalNetRefIndexIncreased(FInternalNetRefIndex NewMaxInternalIndex)
{
	ObjectsToUpdate.SetNumBits(NewMaxInternalIndex);
}

// 设置/重置滞后帧数。已在更新器中则复用 LocalIndex；否则分配并初始化。
void FObjectScopeHysteresisUpdater::SetHysteresisFrameCount(FInternalNetRefIndex NetRefIndex, uint16 HysteresisFrameCount)
{
	const FLocalIndex LocalIndex = GetOrCreateLocalIndex(NetRefIndex);
	FrameCounters[LocalIndex] = HysteresisFrameCount;
}

// 单对象显式移除（O(1) Map 查找）。
void FObjectScopeHysteresisUpdater::RemoveHysteresis(FInternalNetRefIndex NetRefIndex)
{
	if (FLocalIndex* LocalIndex = NetRefIndexToLocalIndex.Find(NetRefIndex))
	{
		FreeLocalIndex(*LocalIndex);
	}
}

/** Removes all objects in the bitarray vfrom hysteresis. */
// 批量按位图移除：与 ObjectsToUpdate 取交集（"既在更新中又被请求移除"）逐个处理。
void FObjectScopeHysteresisUpdater::RemoveHysteresis(const FNetBitArrayView& ObjectsToRemove)
{
	IRIS_PROFILER_SCOPE(FObjectScopeHysteresisUpdater_RemoveHysteresis);
	if (NetRefIndexToLocalIndex.IsEmpty())
	{
		return;
	}

	FNetBitArrayView::ForAllSetBits(MakeNetBitArrayView(ObjectsToUpdate), ObjectsToRemove, FNetBitArrayView::AndOp, [this](uint32 NetRefIndex)
		{
			this->RemoveHysteresis(NetRefIndex);
		}
	);
}

// 批量按数组移除：直接遍历传入索引。
void FObjectScopeHysteresisUpdater::RemoveHysteresis(TArrayView<const uint32> ObjectsToRemove)
{
	IRIS_PROFILER_SCOPE(FObjectScopeHysteresisUpdater_RemoveHysteresis);
	if (NetRefIndexToLocalIndex.IsEmpty())
	{
		return;
	}

	for (const uint32 ObjectIndex : ObjectsToRemove)
	{
		this->RemoveHysteresis(ObjectIndex);
	}
}

// 推进帧数 + 检测过期。优化路径：
//   1) 按 32-bit word 扫 UsedLocalIndices，跳过全 0 段；
//   2) 在每个非零 word 中按 4 位（nibble）切片，再次跳过空 nibble；
//   3) 一次处理 4 个 counter（编译器友好的展开），检测过期值用 FilterOutCompareValue 比较（uint16 wrap-around）。
// 过期对象写入 OutObjectsToFilterOut 并释放 LocalIndex。
void FObjectScopeHysteresisUpdater::Update(uint8 FramesSinceLastUpdate, TArray<FInternalNetRefIndex>& OutObjectsToFilterOut)
{
	IRIS_PROFILER_SCOPE(FObjectScopeHysteresisUpdater_Update);

	ensure(FramesSinceLastUpdate > 0 && FramesSinceLastUpdate <= 128);

	typedef FNetBitArrayView::StorageWordType WordType;
	constexpr uint32 WordBitCount = FNetBitArrayView::WordBitCount;

	// 16-bit 减算下溢检测阈值：counter 减去 FramesSinceLastUpdate 后若 >= 此值，说明在 [-FramesSinceLastUpdate, -1] 范围内（已过期）。
	const uint16 FilterOutCompareValue = (65536U - FramesSinceLastUpdate) & 65535U;
	uint16* CountersData = FrameCounters.GetData();

	TArray<FInternalNetRefIndex, TInlineAllocator<32>> ObjectsToRemoveFromUpdate;

	const WordType* LocalIndicesData = UsedLocalIndices.GetData();
	for (FLocalIndex ObjectIt = 0, ObjectEndIt = UsedLocalIndices.GetNumBits(), IndexOffset = 0; ObjectIt < ObjectEndIt; ObjectIt += WordBitCount, ++LocalIndicesData, IndexOffset += 32U)
	{
		// Skip ranges with no objects to update
		// 整 word 全 0 直接跳过 32 个 LocalIndex。
		WordType LocalIndicesWord = *LocalIndicesData;
		if (!LocalIndicesWord)
		{
			continue;
		}

		WordType LocalIndicesMask = LocalIndicesWord;
		// nibble 级展开：4 位一组，每组对应 4 个 counter。
		for (WordType LocalIndexOffset = 0; LocalIndexOffset < WordBitCount; LocalIndexOffset += 4U, LocalIndicesMask >>= 4U)
		{
			if (!(LocalIndicesMask & 15U))
			{
				continue;
			}

			uint16 Counters[4];
			Counters[0] = CountersData[IndexOffset + LocalIndexOffset + 0];
			Counters[1] = CountersData[IndexOffset + LocalIndexOffset + 1];
			Counters[2] = CountersData[IndexOffset + LocalIndexOffset + 2];
			Counters[3] = CountersData[IndexOffset + LocalIndexOffset + 3];

			Counters[0] -= FramesSinceLastUpdate;
			Counters[1] -= FramesSinceLastUpdate;
			Counters[2] -= FramesSinceLastUpdate;
			Counters[3] -= FramesSinceLastUpdate;

			// We can update the counters regardless of whether the objects were kept in scope or not. We're not expecting calls to Set/Remove while updating.
			// 即便对应槽位实际为 0/未分配，也写回 counter——不在意（Set/Remove 不会在 Update 期间发生）。
			CountersData[IndexOffset + LocalIndexOffset + 0] = Counters[0];
			CountersData[IndexOffset + LocalIndexOffset + 1] = Counters[1];
			CountersData[IndexOffset + LocalIndexOffset + 2] = Counters[2];
			CountersData[IndexOffset + LocalIndexOffset + 3] = Counters[3];

			// For counter values < FramesSinceLastUpdate the object should remain in scope
			// 检测过期：仅看 LocalIndicesMask 当前 nibble 中真正占用的 4 个槽位，遇过期记录到待移除列表。
			for (uint32 Offset : {0, 1, 2, 3})
			{
				if (LocalIndicesMask & (1U << Offset))
				{
					if (Counters[Offset] >= FilterOutCompareValue)
					{
						// Object should now stay filtered out. Remove from updates.
						ObjectsToRemoveFromUpdate.Add(IndexOffset + LocalIndexOffset + Offset);
					}
				}
			}
		}

		// 每处理完一个 word 的 32 个 LocalIndex，把过期对象统一搬到 OutObjectsToFilterOut 并释放 LocalIndex。
		if (!ObjectsToRemoveFromUpdate.IsEmpty())
		{
			OutObjectsToFilterOut.Reserve(OutObjectsToFilterOut.Num() + ObjectsToRemoveFromUpdate.Num());
			for (FLocalIndex LocalIndex : ObjectsToRemoveFromUpdate)
			{
				OutObjectsToFilterOut.Add(LocalIndexToNetRefIndex[LocalIndex]);
				FreeLocalIndex(LocalIndex);
			}

			ObjectsToRemoveFromUpdate.Reset();
		}
	}
}

// 复用或新建 LocalIndex。无空闲位时按 LocalIndexGrowCount 同时扩三个对齐数组。
FObjectScopeHysteresisUpdater::FLocalIndex FObjectScopeHysteresisUpdater::GetOrCreateLocalIndex(FInternalNetRefIndex NetRefIndex)
{
	if (FLocalIndex* LocalIndex = NetRefIndexToLocalIndex.Find(NetRefIndex))
	{
		return *LocalIndex;
	}

	uint32 LocalIndex = UsedLocalIndices.FindFirstZero();
	if (LocalIndex == FNetBitArray::InvalidIndex)
	{
		LocalIndex = UsedLocalIndices.GetNumBits();
		UsedLocalIndices.AddBits(LocalIndexGrowCount);
		LocalIndexToNetRefIndex.AddZeroed(LocalIndexGrowCount);
		FrameCounters.AddZeroed(LocalIndexGrowCount);
	}

	UsedLocalIndices.SetBit(LocalIndex);
	LocalIndexToNetRefIndex[LocalIndex] = NetRefIndex;
	NetRefIndexToLocalIndex.Add(NetRefIndex, LocalIndex);
	ObjectsToUpdate.SetBit(NetRefIndex);

	return LocalIndex;
}

/** Removes all objects in the bitarray vfrom hysteresis. */
void FObjectScopeHysteresisUpdater::RemoveHysteresis(const FNetBitArrayView& ObjectsToRemove)
{
	IRIS_PROFILER_SCOPE(FObjectScopeHysteresisUpdater_RemoveHysteresis);
	if (NetRefIndexToLocalIndex.IsEmpty())
	{
		return;
	}

	FNetBitArrayView::ForAllSetBits(MakeNetBitArrayView(ObjectsToUpdate), ObjectsToRemove, FNetBitArrayView::AndOp, [this](uint32 NetRefIndex)
		{
			this->RemoveHysteresis(NetRefIndex);
		}
	);
}

void FObjectScopeHysteresisUpdater::RemoveHysteresis(TArrayView<const uint32> ObjectsToRemove)
{
	IRIS_PROFILER_SCOPE(FObjectScopeHysteresisUpdater_RemoveHysteresis);
	if (NetRefIndexToLocalIndex.IsEmpty())
	{
		return;
	}

	for (const uint32 ObjectIndex : ObjectsToRemove)
	{
		this->RemoveHysteresis(ObjectIndex);
	}
}

void FObjectScopeHysteresisUpdater::Update(uint8 FramesSinceLastUpdate, TArray<FInternalNetRefIndex>& OutObjectsToFilterOut)
{
	IRIS_PROFILER_SCOPE(FObjectScopeHysteresisUpdater_Update);

	ensure(FramesSinceLastUpdate > 0 && FramesSinceLastUpdate <= 128);

	typedef FNetBitArrayView::StorageWordType WordType;
	constexpr uint32 WordBitCount = FNetBitArrayView::WordBitCount;

	const uint16 FilterOutCompareValue = (65536U - FramesSinceLastUpdate) & 65535U;
	uint16* CountersData = FrameCounters.GetData();

	TArray<FInternalNetRefIndex, TInlineAllocator<32>> ObjectsToRemoveFromUpdate;

	const WordType* LocalIndicesData = UsedLocalIndices.GetData();
	for (FLocalIndex ObjectIt = 0, ObjectEndIt = UsedLocalIndices.GetNumBits(), IndexOffset = 0; ObjectIt < ObjectEndIt; ObjectIt += WordBitCount, ++LocalIndicesData, IndexOffset += 32U)
	{
		// Skip ranges with no objects to update
		WordType LocalIndicesWord = *LocalIndicesData;
		if (!LocalIndicesWord)
		{
			continue;
		}

		WordType LocalIndicesMask = LocalIndicesWord;
		for (WordType LocalIndexOffset = 0; LocalIndexOffset < WordBitCount; LocalIndexOffset += 4U, LocalIndicesMask >>= 4U)
		{
			if (!(LocalIndicesMask & 15U))
			{
				continue;
			}

			uint16 Counters[4];
			Counters[0] = CountersData[IndexOffset + LocalIndexOffset + 0];
			Counters[1] = CountersData[IndexOffset + LocalIndexOffset + 1];
			Counters[2] = CountersData[IndexOffset + LocalIndexOffset + 2];
			Counters[3] = CountersData[IndexOffset + LocalIndexOffset + 3];

			Counters[0] -= FramesSinceLastUpdate;
			Counters[1] -= FramesSinceLastUpdate;
			Counters[2] -= FramesSinceLastUpdate;
			Counters[3] -= FramesSinceLastUpdate;

			// We can update the counters regardless of whether the objects were kept in scope or not. We're not expecting calls to Set/Remove while updating.
			CountersData[IndexOffset + LocalIndexOffset + 0] = Counters[0];
			CountersData[IndexOffset + LocalIndexOffset + 1] = Counters[1];
			CountersData[IndexOffset + LocalIndexOffset + 2] = Counters[2];
			CountersData[IndexOffset + LocalIndexOffset + 3] = Counters[3];

			// For counter values < FramesSinceLastUpdate the object should remain in scope
			for (uint32 Offset : {0, 1, 2, 3})
			{
				if (LocalIndicesMask & (1U << Offset))
				{
					if (Counters[Offset] >= FilterOutCompareValue)
					{
						// Object should now stay filtered out. Remove from updates.
						ObjectsToRemoveFromUpdate.Add(IndexOffset + LocalIndexOffset + Offset);
					}
				}
			}
		}

		if (!ObjectsToRemoveFromUpdate.IsEmpty())
		{
			OutObjectsToFilterOut.Reserve(OutObjectsToFilterOut.Num() + ObjectsToRemoveFromUpdate.Num());
			for (FLocalIndex LocalIndex : ObjectsToRemoveFromUpdate)
			{
				OutObjectsToFilterOut.Add(LocalIndexToNetRefIndex[LocalIndex]);
				FreeLocalIndex(LocalIndex);
			}

			ObjectsToRemoveFromUpdate.Reset();
		}
	}
}

FObjectScopeHysteresisUpdater::FLocalIndex FObjectScopeHysteresisUpdater::GetOrCreateLocalIndex(FInternalNetRefIndex NetRefIndex)
{
	if (FLocalIndex* LocalIndex = NetRefIndexToLocalIndex.Find(NetRefIndex))
	{
		return *LocalIndex;
	}

	uint32 LocalIndex = UsedLocalIndices.FindFirstZero();
	if (LocalIndex == FNetBitArray::InvalidIndex)
	{
		LocalIndex = UsedLocalIndices.GetNumBits();
		UsedLocalIndices.AddBits(LocalIndexGrowCount);
		LocalIndexToNetRefIndex.AddZeroed(LocalIndexGrowCount);
		FrameCounters.AddZeroed(LocalIndexGrowCount);
	}

	UsedLocalIndices.SetBit(LocalIndex);
	LocalIndexToNetRefIndex[LocalIndex] = NetRefIndex;
	NetRefIndexToLocalIndex.Add(NetRefIndex, LocalIndex);
	ObjectsToUpdate.SetBit(NetRefIndex);

	return LocalIndex;
}

// 释放 LocalIndex：仅清位图与反查 Map；LocalIndexToNetRefIndex 不维护"未占用槽位"的内容（节省一次写）。
void FObjectScopeHysteresisUpdater::FreeLocalIndex(FLocalIndex LocalIndex)
{
	UsedLocalIndices.ClearBit(LocalIndex);
	const FInternalNetRefIndex NetRefIndex = LocalIndexToNetRefIndex[LocalIndex];
	NetRefIndexToLocalIndex.Remove(NetRefIndex);
	ObjectsToUpdate.ClearBit(NetRefIndex);
	// Intentionally not updating LocalIndexToNetRefIndex since it isn't accessed for unset local indices.
}

}

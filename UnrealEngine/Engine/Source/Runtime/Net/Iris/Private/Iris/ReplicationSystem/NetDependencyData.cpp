// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================================================
// NetDependencyData.cpp —— 邻接表实现
// -------------------------------------------------------------------------------------------------------------
// 关键约束：
//   - SubObjectConditionalsArray 必须与 ChildSubObjects 数组保持 1:1 对齐（每个 Child 一条 condition）；
//     因此 GetOrCreateSubObjectConditionalsArray 强制要求 ChildSubObjects 已存在并自动 SetNumZeroed 同步长度。
//   - FreeStoredDependencyDataForObject 期望各表已为空（checkSlow），调用者负责先 RemoveSwap 元素。
// =============================================================================================================

#include "NetDependencyData.h"
#include "Iris/Core/IrisLog.h"
#include "Iris/ReplicationSystem/ReplicationOperationsInternal.h"
#include "UObject/CoreNetTypes.h"

namespace UE::Net::Private
{

FNetDependencyData::FNetDependencyData()
{
}

// 释放某对象的全部依赖元数据 + 5 个 SparseArray 槽位。
// 调用前各 InternalIndexArray 应为空，否则代表"还有依赖关系尚未解除"，是上游逻辑错误。
void FNetDependencyData::FreeStoredDependencyDataForObject(FInternalNetRefIndex InternalIndex)
{
	if (FDependencyInfo* Entry = DependencyInfos.Find(InternalIndex))
	{
		// 释放 4 类 InternalIndexArray（SubObjects / Child / Parent / CreationDeps）。
		for (const uint32 ArrayIndex : Entry->ArrayIndices)
		{
			if (ArrayIndex != FDependencyInfo::InvalidCacheIndex)
			{
				checkSlow(DependentObjectsStorage[ArrayIndex].Num() == 0);
				DependentObjectsStorage.RemoveAt(ArrayIndex);
			}
		}

		// 单独释放：SubObject conditionals / DependentObjectInfo / CreationDependencyInfo。
		if (Entry->SubObjectConditionalArrayIndex != FDependencyInfo::InvalidCacheIndex)
		{
			SubObjectConditionalsStorage.RemoveAt(Entry->SubObjectConditionalArrayIndex);
		}

		if (Entry->DependentObjectsInfoArrayIndex != FDependencyInfo::InvalidCacheIndex)
		{
			DependentObjectInfosStorage.RemoveAt(Entry->DependentObjectsInfoArrayIndex);
		}

		if (Entry->CreationDependencyArrayIndex != FDependencyInfo::InvalidCacheIndex)
		{
			CreationDependencyInfosStorage.RemoveAt(Entry->CreationDependencyArrayIndex);
		}

		DependencyInfos.Remove(InternalIndex);
	}
}

// 内部：拿到某对象的 FDependencyInfo（不存在则懒分配，所有索引为 InvalidCacheIndex）。
FNetDependencyData::FDependencyInfo& FNetDependencyData::GetOrCreateCacheEntry(FInternalNetRefIndex InternalIndex)
{
	FDependencyInfo* Entry = DependencyInfos.Find(InternalIndex);

	if (!Entry)
	{
		Entry = &DependencyInfos.Add(InternalIndex);
		*Entry = FDependencyInfo();
	}

	return *Entry;
}

// 在 Parent 一侧创建/返回"我有哪些 dependent child"列表（含 SchedulingHint）。
FNetDependencyData::FDependentObjectInfoArray& FNetDependencyData::GetOrCreateDependentObjectInfoArray(FInternalNetRefIndex OwnerIndex)
{
	FDependencyInfo& Entry = GetOrCreateCacheEntry(OwnerIndex);
	
	if (Entry.DependentObjectsInfoArrayIndex == FDependencyInfo::InvalidCacheIndex)
	{
		FSparseArrayAllocationInfo AllocInfo = DependentObjectInfosStorage.AddUninitialized();
		Entry.DependentObjectsInfoArrayIndex = AllocInfo.Index;

		// placement new：FSparseArrayAllocationInfo.Pointer 是未构造的内存，需要原地构造。
		FDependentObjectInfoArray* DependentObjectsInfoArrayIndex = new (AllocInfo.Pointer) FDependentObjectInfoArray();
		
		return *DependentObjectsInfoArrayIndex;
	}
	else
	{
		return DependentObjectInfosStorage[Entry.DependentObjectsInfoArrayIndex];
	}
}

// 创建/返回 SubObject 的 LifetimeCondition 数组；
// 强制要求 ChildSubObjects 已存在，并按其长度初始化 conditionals（COND_None=0，可用 SetNumZeroed）。
FNetDependencyData::FSubObjectConditionalsArray& FNetDependencyData::GetOrCreateSubObjectConditionalsArray(FInternalNetRefIndex OwnerIndex)
{
	FDependencyInfo& Entry = GetOrCreateCacheEntry(OwnerIndex);
	check(Entry.ArrayIndices[EArrayType::ChildSubObjects] != FDependencyInfo::InvalidCacheIndex);
	
	if (Entry.SubObjectConditionalArrayIndex == FDependencyInfo::InvalidCacheIndex)
	{
		FSparseArrayAllocationInfo AllocInfo = SubObjectConditionalsStorage.AddUninitialized();
		Entry.SubObjectConditionalArrayIndex = AllocInfo.Index;

		FSubObjectConditionalsArray* SubObjectConditionalsArray = new (AllocInfo.Pointer) FSubObjectConditionalsArray();
		
		// Make sure that we initialize the conditionals to match the number of SubObjects
		// 与 ChildSubObjects 数组保持 1:1 对齐（每个 child 一条 condition）。
		const int32 NumChildSubObjects = DependentObjectsStorage[Entry.ArrayIndices[EArrayType::ChildSubObjects]].Num();
		static_assert(COND_None == 0, "Can't use SetNumZeroed() to initialize COND_None");
		SubObjectConditionalsArray->SetNumZeroed(NumChildSubObjects);

		return *SubObjectConditionalsArray;
	}
	else
	{
		return SubObjectConditionalsStorage[Entry.SubObjectConditionalArrayIndex];
	}
}

// 一次拿到 ChildSubObjects 数组（创建/复用）+ 关联的 SubObjectConditionals 指针（不创建 conditionals）。
// 这样调用者添加 SubObject 时可以延迟决定要不要给它配 condition。
FNetDependencyData::FInternalNetRefIndexArray& FNetDependencyData::GetOrCreateInternalChildSubObjectsArray(FInternalNetRefIndex OwnerIndex, FSubObjectConditionalsArray*& OutSubObjectConditionals)
{
	FDependencyInfo& Entry = GetOrCreateCacheEntry(OwnerIndex);
	if (Entry.ArrayIndices[ChildSubObjects] == FDependencyInfo::InvalidCacheIndex)
	{
		// Allocate storage for subobjects / atomic dependencies
		FSparseArrayAllocationInfo AllocInfo = DependentObjectsStorage.AddUninitialized();
		Entry.ArrayIndices[ChildSubObjects] = AllocInfo.Index;

		FInternalNetRefIndexArray* InternalIndexArray = new (AllocInfo.Pointer) FInternalNetRefIndexArray();
		OutSubObjectConditionals = nullptr;
		return *InternalIndexArray;
	}
	else
	{
		// 已存在 ChildSubObjects 数组：返回它，并把 conditionals 指针一并输出（可能为 nullptr）。
		OutSubObjectConditionals = Entry.SubObjectConditionalArrayIndex != FDependencyInfo::InvalidCacheIndex ? &SubObjectConditionalsStorage[Entry.SubObjectConditionalArrayIndex] : nullptr;
		return DependentObjectsStorage[Entry.ArrayIndices[ChildSubObjects]];
	}
}


// CreationDependency：在 Child 一侧记录"创建本对象前必须先存在哪些 Parent"。
FNetDependencyData::FCreationDependencyInfoArray& FNetDependencyData::GetOrCreateCreationDependencyInfoArray(FInternalNetRefIndex ChildIndex)
{
	FDependencyInfo& Entry = GetOrCreateCacheEntry(ChildIndex);
	
	if (Entry.CreationDependencyArrayIndex == FDependencyInfo::InvalidCacheIndex)
	{
		FSparseArrayAllocationInfo AllocInfo = CreationDependencyInfosStorage.AddUninitialized();
		Entry.CreationDependencyArrayIndex = AllocInfo.Index;

		FCreationDependencyInfoArray* InfoArray = new (AllocInfo.Pointer) FCreationDependencyInfoArray();
		
		return *InfoArray;
	}
	else
	{
		return CreationDependencyInfosStorage[Entry.CreationDependencyArrayIndex];
	}
}

// 释放 Child 的 creation dependency 数组（一般在依赖解除完成后调用）。
void FNetDependencyData::FreeCreationDependencyInfoArray(FInternalNetRefIndex ChildIndex)
{
	if (FDependencyInfo* Entry = DependencyInfos.Find(ChildIndex))
	{
		if (Entry->CreationDependencyArrayIndex != FDependencyInfo::InvalidCacheIndex)
		{
			CreationDependencyInfosStorage.RemoveAt(Entry->CreationDependencyArrayIndex);
			Entry->CreationDependencyArrayIndex = FDependencyInfo::InvalidCacheIndex;
		}
	}
}

// 通用版：按 EArrayType 获取/创建 InternalIndexArray（SubObjects / DependentParents 等共享同一池）。
FNetDependencyData::FInternalNetRefIndexArray& FNetDependencyData::GetOrCreateInternalIndexArray(FInternalNetRefIndex OwnerIndex, EArrayType ArrayType)
{
	FDependencyInfo& Entry = GetOrCreateCacheEntry(OwnerIndex);
	if (Entry.ArrayIndices[ArrayType] == FDependencyInfo::InvalidCacheIndex)
	{
		// Allocate storage for subobjects / atomic dependencies
		FSparseArrayAllocationInfo AllocInfo = DependentObjectsStorage.AddUninitialized();
		Entry.ArrayIndices[ArrayType] = AllocInfo.Index;

		FInternalNetRefIndexArray* InternalIndexArray = new (AllocInfo.Pointer) FInternalNetRefIndexArray();

		return *InternalIndexArray;
	}
	else
	{
		return DependentObjectsStorage[Entry.ArrayIndices[ArrayType]];
	}
}

}
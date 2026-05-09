// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================================================
// NetDependencyData.h —— 复制对象的"邻接表"集合
// -------------------------------------------------------------------------------------------------------------
// 模块定位：以 FInternalNetRefIndex 为 key，集中维护 4 类对象间拓扑关系：
//
//   ① SubObjects                  ：拥有者关系。Owner.SubObjects = [Child0, Child1, ...]
//                                   （SubObject 必须随 Owner 一起 Scope/Filter）
//
//   ② ChildSubObjects             ：和 SubObjects 类似，但额外保存了每条 SubObject 的 ELifetimeCondition
//                                   （COND_OwnerOnly / COND_AutonomousOnly 等），用于按连接条件过滤。
//
//   ③ DependentParentObjects      ：反向引用。Child.DependentParents = [P0, P1, ...] 表示 Child 依赖这些 Parent
//                                   先一起被复制（用于"附属对象"）。配合 DependentObjectInfos（在 Parent 端）使用。
//
//   ④ CreationDependencies        ：Child 创建时所必须的前置 Parent。Reader 端只有当 CreationDependentParents
//                                   都已实例化后，才能解码 Child 的 creation header（详见 PendingBatchData）。
//
// 内存布局：
//   - DependencyInfos: TMap<InternalIndex, FDependencyInfo>，仅"有依赖关系"的对象才分配（节省内存）。
//   - 4 条 SparseArray 池存放变长子表，FDependencyInfo 中存放各表的 SparseArray 索引。
//
// 使用方：FNetRefHandleManager 的 AddSubObjectToOwner / AddDependentObject / AddCreationDependency 等。
// =============================================================================================================

#pragma once

#include "CoreTypes.h"
#include "Containers/ArrayView.h"
#include "Containers/Map.h"
#include "Containers/SparseArray.h"
#include "Iris/ReplicationSystem/ReplicationSystemTypes.h"

namespace UE::Net::Private
{
	typedef uint32 FInternalNetRefIndex;
	typedef int8 FLifeTimeConditionStorage; // 与 ELifetimeCondition 等宽（COND_None=0 等）。
}

namespace UE::Net::Private
{

// 只读视图：调用方一次拿到 ChildSubObjects + 每个 SubObject 的 LifetimeCondition + 数量。
struct FChildSubObjectsInfo
{
	const FInternalNetRefIndex* ChildSubObjects = nullptr;
	const FLifeTimeConditionStorage* SubObjectLifeTimeConditions = nullptr;
	uint32 NumSubObjects = 0U;
};

// Parent 上记录的"依赖于本 Parent"的子对象信息（含调度提示）。
struct FDependentObjectInfo
{
	FInternalNetRefIndex NetRefIndex = 0U;
	EDependentObjectSchedulingHint SchedulingHint = EDependentObjectSchedulingHint::Default;
};

class FNetDependencyData
{
public:
	FNetDependencyData();

	// 各表的存储容器：使用 TInlineAllocator 优化"小 Owner"场景。
	typedef TArray<FInternalNetRefIndex, TInlineAllocator<8>> FInternalNetRefIndexArray;
	typedef TArray<FLifeTimeConditionStorage, TInlineAllocator<8>> FSubObjectConditionalsArray;
	typedef TArray<FDependentObjectInfo, TInlineAllocator<8>> FDependentObjectInfoArray;
	typedef TArray<FInternalNetRefIndex, TInlineAllocator<2>> FCreationDependencyInfoArray;

	// 4 类拓扑表的索引枚举（前 3 类可以模板化访问）。
	enum EArrayType
	{
		SubObjects = 0U,
		ChildSubObjects,
		DependentParentObjects,
		CreationDependencies,
		Count
	};

	// 模板版便捷接口：按 EArrayType 拿到对应的 InternalIndex 数组（不存在则创建）。
	template<EArrayType TypeIndex>
	FInternalNetRefIndexArray& GetOrCreateInternalIndexArray(FInternalNetRefIndex InternalIndex)
	{
		static_assert(TypeIndex != EArrayType::Count, "Invalid array type index");
		return GetOrCreateInternalIndexArray(InternalIndex, TypeIndex);
	};

	// DependentObjectInfo 单独走一套（含 SchedulingHint）：
	//   - 存储位置：Parent 一侧（即"我有哪些 dependent child"）。
	FDependentObjectInfoArray& GetOrCreateDependentObjectInfoArray(FInternalNetRefIndex InternalIndex);
	FDependentObjectInfoArray* GetDependentObjectInfoArray(FInternalNetRefIndex InternalIndex)
	{
		const FDependencyInfo* Entry = DependencyInfos.Find(InternalIndex);
		const uint32 ArrayIndex = Entry ? Entry->DependentObjectsInfoArrayIndex : FDependencyInfo::InvalidCacheIndex;
		if (ArrayIndex != FDependencyInfo::InvalidCacheIndex)
		{
			return &DependentObjectInfosStorage[ArrayIndex];
		}
		return nullptr;
	}

	TArrayView<const FDependentObjectInfo> GetDependentObjectInfoArray(FInternalNetRefIndex InternalIndex) const
	{
		const FDependencyInfo* Entry = DependencyInfos.Find(InternalIndex);
		const uint32 ArrayIndex = Entry ? Entry->DependentObjectsInfoArrayIndex : FDependencyInfo::InvalidCacheIndex;
		if (ArrayIndex != FDependencyInfo::InvalidCacheIndex)
		{
			return MakeArrayView(DependentObjectInfosStorage[ArrayIndex]);
		}
		return MakeArrayView<const FDependentObjectInfo>(nullptr, 0);
	}

	// SubObject Conditionals 必须在 ChildSubObjects 数组已存在的前提下才能使用（保持 1:1 对齐）。
	FSubObjectConditionalsArray& GetOrCreateSubObjectConditionalsArray(FInternalNetRefIndex InternalIndex);

	// 同时拿到 ChildSubObjects 数组和 conditionals 数组（输入：Owner 的 InternalIndex）。
	FInternalNetRefIndexArray& GetOrCreateInternalChildSubObjectsArray(FInternalNetRefIndex InternalIndex, FSubObjectConditionalsArray*& OutSubObjectConditionals);

	// 只读访问：返回是否存在 ChildSubObjects 数组；存在时通过输出参数返回数组指针 + conditionals 指针。
	bool GetInternalChildSubObjectAndConditionalArrays(FInternalNetRefIndex InternalIndex, FInternalNetRefIndexArray*& OutChildSubObjectsArray, FSubObjectConditionalsArray*& OutSubObjectConditionals)
	{
		const FDependencyInfo* Entry = DependencyInfos.Find(InternalIndex);
		const uint32 ArrayIndex = Entry ? Entry->ArrayIndices[ChildSubObjects] : FDependencyInfo::InvalidCacheIndex;

		if (ArrayIndex == FDependencyInfo::InvalidCacheIndex)
		{
			return false;
		}

		OutChildSubObjectsArray = &DependentObjectsStorage[ArrayIndex];
		OutSubObjectConditionals = Entry->SubObjectConditionalArrayIndex != FDependencyInfo::InvalidCacheIndex ? &SubObjectConditionalsStorage[Entry->SubObjectConditionalArrayIndex] : nullptr;
		return true;
	}

	// 高频路径：一次性返回 (子对象指针 + 生命周期条件 + 数量)，供 Filtering / Conditionals 子系统枚举。
	bool GetChildSubObjects(FInternalNetRefIndex InternalIndex, FChildSubObjectsInfo& OutInfo) const
	{
		const FDependencyInfo* Entry = DependencyInfos.Find(InternalIndex);
		const uint32 ArrayIndex = Entry ? Entry->ArrayIndices[ChildSubObjects] : FDependencyInfo::InvalidCacheIndex;
		if (ArrayIndex == FDependencyInfo::InvalidCacheIndex)
		{
			return false;
		}

		OutInfo.ChildSubObjects = DependentObjectsStorage[ArrayIndex].GetData();			
		OutInfo.SubObjectLifeTimeConditions = Entry->SubObjectConditionalArrayIndex != FDependencyInfo::InvalidCacheIndex ? SubObjectConditionalsStorage[Entry->SubObjectConditionalArrayIndex].GetData() : nullptr;
		OutInfo.NumSubObjects = DependentObjectsStorage[ArrayIndex].Num();
		return true;
	}

	/** Create or return the creation dependency list of an object */
	// CreationDependency：存在 Child 一侧——"我创建前需要哪些 Parent 先存在"。
	FCreationDependencyInfoArray& GetOrCreateCreationDependencyInfoArray(FInternalNetRefIndex ChildIndex);
	
	/** Liberate the creation dependency list assigned to an object */
	void FreeCreationDependencyInfoArray(FInternalNetRefIndex ChildIndex);

	FCreationDependencyInfoArray* GetCreationDependencyInfoArray(FInternalNetRefIndex ChildIndex)
	{
		const FDependencyInfo* Entry = DependencyInfos.Find(ChildIndex);
		const uint32 ArrayIndex = Entry ? Entry->CreationDependencyArrayIndex : FDependencyInfo::InvalidCacheIndex;
		if (ArrayIndex != FDependencyInfo::InvalidCacheIndex)
		{
			return &CreationDependencyInfosStorage[ArrayIndex];
		}
		return nullptr;
	}

	const FCreationDependencyInfoArray* GetCreationDependencyInfoArray(FInternalNetRefIndex ChildIndex) const
	{
		const FDependencyInfo* Entry = DependencyInfos.Find(ChildIndex);
		const uint32 ArrayIndex = Entry ? Entry->CreationDependencyArrayIndex : FDependencyInfo::InvalidCacheIndex;
		if (ArrayIndex != FDependencyInfo::InvalidCacheIndex)
		{
			return &CreationDependencyInfosStorage[ArrayIndex];
		}
		return nullptr;
	}

	// 模板版只读访问：拿到指定类型表（指针/视图）。
	template<EArrayType TypeIndex>
	FInternalNetRefIndexArray* GetInternalIndexArray(FInternalNetRefIndex InternalIndex)
	{
		static_assert(TypeIndex != EArrayType::Count, "Invalid array type index");
		const FDependencyInfo* Entry = DependencyInfos.Find(InternalIndex);
		const uint32 ArrayIndex = Entry ? Entry->ArrayIndices[TypeIndex] : FDependencyInfo::InvalidCacheIndex;
		if (ArrayIndex != FDependencyInfo::InvalidCacheIndex)
		{
			return &DependentObjectsStorage[ArrayIndex];
		}
		return nullptr;
	}

	template<EArrayType TypeIndex>
	TArrayView<const FInternalNetRefIndex> GetInternalIndexArray(FInternalNetRefIndex InternalIndex) const
	{
		static_assert(TypeIndex != EArrayType::Count, "Invalid array type index");

		const FDependencyInfo* Entry = DependencyInfos.Find(InternalIndex);
		const uint32 ArrayIndex = Entry ? Entry->ArrayIndices[TypeIndex] : FDependencyInfo::InvalidCacheIndex;
		if (ArrayIndex != FDependencyInfo::InvalidCacheIndex)
		{
			return MakeArrayView(DependentObjectsStorage[ArrayIndex]);
		}
		return MakeArrayView<const FInternalNetRefIndex>(nullptr, 0);
	}

	// 释放某对象的全部 5 类依赖数据（销毁 NetRefHandle 时调用）。
	// 调用前应保证各 InternalIndexArray 已清空（checkSlow），仅释放 SparseArray 槽位。
	void FreeStoredDependencyDataForObject(FInternalNetRefIndex InternalIndex);
	
private:
	
	// 单个对象的依赖元信息：存放各表在 SparseArray 池中的下标（InvalidCacheIndex = 不存在）。
	struct FDependencyInfo
	{
		constexpr static uint32 InvalidCacheIndex = ~(0U);
		uint32 ArrayIndices[EArrayType::Count] = { InvalidCacheIndex, InvalidCacheIndex, InvalidCacheIndex, InvalidCacheIndex }; // SubObjects/Child/Parent/CreationDeps 的 SparseArray 下标
		uint32 SubObjectConditionalArrayIndex = InvalidCacheIndex;       // SubObjectConditionalsStorage 下标
		uint32 DependentObjectsInfoArrayIndex = InvalidCacheIndex;       // DependentObjectInfosStorage 下标
		uint32 CreationDependencyArrayIndex = InvalidCacheIndex;         // CreationDependencyInfosStorage 下标
	};

private:
	FInternalNetRefIndexArray& GetOrCreateInternalIndexArray(FInternalNetRefIndex InternalIndex, EArrayType Type);
	FDependencyInfo& GetOrCreateCacheEntry(FInternalNetRefIndex InternalIndex);
	
	// Map to track the replicated objects with subObjects or dependencies
	// 仅记录"有 SubObjects/Dependents/CreationDeps"的对象，节省内存。
	TMap<FInternalNetRefIndex, FDependencyInfo> DependencyInfos;

	// Storage for DependentObjects and SubObjects
	// 共享池：SubObjects / ChildSubObjects / DependentParentObjects / CreationDependencies 都从这里分配槽位。
	TSparseArray<FInternalNetRefIndexArray> DependentObjectsStorage;

	// Storage for SubObject conditionals
	// ChildSubObjects 对齐的 LifetimeCondition 数组。
	TSparseArray<FSubObjectConditionalsArray> SubObjectConditionalsStorage;

	// Storage for DependentObjects traits and info (bound on the Parent)
	// Parent 端记录"我有哪些 dependent child + 调度提示"。
	TSparseArray<FDependentObjectInfoArray> DependentObjectInfosStorage;

	// Storage for creation dependency infos (bound on the Child)
	// Child 端记录"我创建前需要哪些 Parent"。
	TSparseArray<FCreationDependencyInfoArray> CreationDependencyInfosStorage;

};

}

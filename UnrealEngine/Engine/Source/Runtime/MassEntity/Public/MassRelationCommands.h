// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassCommands.h"
#include "MassEntityManager.h"

// 【中文】调试命名宏（同 MassCommands.h）：仅 Debug / CSV 构建启用 DebugName 参数。
#if CSV_PROFILER_STATS || WITH_MASSENTITY_DEBUG
#	define DEBUG_NAME(Name) , FName(TEXT(Name))
#	define DEBUG_NAME_PARAM(Name) , const FName InDebugName = TEXT(Name)
#	define FORWARD_DEBUG_NAME_PARAM , InDebugName
#else
#	define DEBUG_NAME(Name)
#	define DEBUG_NAME_PARAM(Name)
#	define FORWARD_DEBUG_NAME_PARAM
#endif // CSV_PROFILER_STATS || WITH_MASSENTITY_DEBUG

// 【中文】"延迟创建 entity 间关系 (Relation)"的批量命令模板。
// TRelation 是关系类型（编译期决定，例如 FParentChildRelation）。每条命令实例
// 累积一组 (Child, Parent) 对，Flush 时调用 EntityManager.BatchCreateRelations 一次性建立。
//
// 内部用两组并行数组：
//   TargetEntities[i]  ← 第 i 个子 entity（继承自 FMassBatchedEntityCommand）
//   Parents[i]         ← 对应的父 entity
// 两数组同长，逐位配对。
//
// OperationType 默认 Add（关系视为对 child 的"附加"语义），但用户可在 ctor 改写。
template<typename TRelation>
struct FMassCommandMakeRelation : public FMassBatchedEntityCommand
{
	using Super = FMassBatchedEntityCommand;

	FMassCommandMakeRelation(EMassCommandOperationType OperationType = EMassCommandOperationType::Add DEBUG_NAME_PARAM("MakeRelation"))
		: Super(OperationType FORWARD_DEBUG_NAME_PARAM)
	{}

	// 【中文】单对 (child, parent) 入队：复用基类 Add(child) 把子 entity 接到 TargetEntities，
	// 同时把 parent 推入并行数组 Parents。
	void Add(const FMassEntityHandle ChildEntity, const FMassEntityHandle ParentEntity)
	{
		Super::Add(ChildEntity);
		Parents.Add(ParentEntity);
	}

	// 【中文】"多对多"批量入队：
	//   - 当 children 数 > parents 数：把 ParentEntities 按需循环复用，最后裁到 children 同长
	//     （相当于"多个孩子共享同一组父循环分配"）；
	//   - 当 children 数 ≤ parents 数：取 min(parents, children) 长度的 parent 段，
	//     一一配对（多余 parent 被丢弃）。
	// 设计 @todo：父=1、子=N 这种"多孩子共享同一父"的常见情况其实可以走更高效的专用路径。
	void Add(TConstArrayView<FMassEntityHandle> ChildEntities, TConstArrayView<FMassEntityHandle> ParentEntities)
	{
		ensure(ChildEntities.Num() != 0 && ParentEntities.Num() != 0);

		Super::Add(ChildEntities);

		if (ChildEntities.Num() > ParentEntities.Num())
		{
			// @todo to be improved - we should have a dedicated path for Multi-children -> Single parent  operations
			// 【中文】循环 Append 父数组直到长度足够，再裁到与孩子同长。
			do
			{
				Parents.Append(ParentEntities.GetData(), ParentEntities.Num());
			} while (Parents.Num() < TargetEntities.Num());
			Parents.SetNum(TargetEntities.Num(), EAllowShrinking::No);
		}
		else
		{
			Parents.Append(ParentEntities.GetData(), FMath::Min(ParentEntities.Num(), ChildEntities.Num()));
		}
	}

protected:
	// 【中文】Reset：清空父数组（基类清空子数组）。
	virtual void Reset() override
	{
		Parents.Reset();
		Super::Reset();
	}

	virtual SIZE_T GetAllocatedSize() const override
	{
		return Super::GetAllocatedSize() + Parents.GetAllocatedSize();
	}

	// 【中文】Run：把累积的 (child, parent) 对一次性交给 EntityManager。
	// BatchCreateRelations 内部按关系类型 TRelation 走类型化的快路径。
	virtual void Run(FMassEntityManager& EntityManager) override
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FMassCommandMakeRelation_Execute);
		EntityManager.BatchCreateRelations<TRelation>(TargetEntities, Parents);
	}

	// 【中文】与 TargetEntities 并行的父 entity 列表。
	TArray<FMassEntityHandle> Parents;
};

#undef DEBUG_NAME
#undef DEBUG_NAME_PARAM
#undef FORWARD_DEBUG_NAME_PARAM

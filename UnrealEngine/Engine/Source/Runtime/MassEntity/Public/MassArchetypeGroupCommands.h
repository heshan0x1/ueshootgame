// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassArchetypeGroup.h"
#include "MassCommands.h"

// 【中文】调试命名宏：与 MassCommands.h 中含义一致 —— 仅在 CSV / Debug 构建下，
// 命令构造时给基类传一个可读 FName 用于 profiler 与断点系统识别。
#if CSV_PROFILER_STATS || WITH_MASSENTITY_DEBUG
#	define DEBUG_NAME(Name) , FName(TEXT(Name))
#	define DEBUG_NAME_PARAM(Name) , const FName InDebugName = TEXT(Name)
#	define FORWARD_DEBUG_NAME_PARAM , InDebugName
#else
#	define DEBUG_NAME(Name)
#	define DEBUG_NAME_PARAM(Name)
#	define FORWARD_DEBUG_NAME_PARAM
#endif // CSV_PROFILER_STATS || WITH_MASSENTITY_DEBUG

namespace UE::Mass::Command
{
	//-----------------------------------------------------------------------------
	// FGroupEntities
	//-----------------------------------------------------------------------------
	// 【中文】"把若干 entity 划入指定 ArchetypeGroup"的批量命令。
	// 背景：archetype group 是 Mass 的逻辑分组机制（同 archetype 内细分），entity 进入
	// / 离开一个 group 等同于 archetype 切换。这里把变更累积起来 → Flush 时一次性落实。
	//
	// 累积结构：TMap<GroupHandle, TArray<EntityHandle>> —— 同一个 group 的多次 push
	// 会被合并到同一个 array，最终对每个 group 调用一次 BatchGroupEntities。
	// OperationType=ChangeComposition 因为 group 切换会改 archetype。
	struct FGroupEntities : public FMassBatchedEntityCommand
	{
		using Super = FMassBatchedEntityCommand;
		FGroupEntities()
			: Super(EMassCommandOperationType::ChangeComposition DEBUG_NAME("GroupEntities"))
		{}

		// 【中文】把一组 entity 加入到 GroupHandle 所表示的分组。
		// 同一 GroupHandle 多次 Add 会自动合并（FindOrAdd 后 Append）。
		// 注意：这里直接重载了基类的 Add(...)，并不调用 Super::Add；
		// TargetEntities（来自基类）实际上没被填充——本命令用 Groups map 自管，
		// HasWork 也不会被基类逻辑置为 true。⚠ 见末尾"疑似 bug"汇总。
		void Add(FArchetypeGroupHandle GroupHandle, TArray<FMassEntityHandle>&& Entities)
		{
			Groups.FindOrAdd(GroupHandle).Append(MoveTemp(Entities));
		}

	protected:
		virtual void Run(FMassEntityManager& EntityManager) override
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(MassCommandGroupEntities_Execute);
			// 【中文】对每个 group：把它的 entity 列表按 archetype 折叠成 collections，
			// 再调 BatchGroupEntities 一次性对该 group 做归类。
			for (auto Group : Groups)
			{
				TArray<FMassArchetypeEntityCollection> CollectionsAtLevel;
				UE::Mass::Utils::CreateEntityCollections(EntityManager, Group.Value
					, FMassArchetypeEntityCollection::EDuplicatesHandling::FoldDuplicates
					, CollectionsAtLevel);

				EntityManager.BatchGroupEntities(Group.Key, CollectionsAtLevel);
			}
		}

		// 【中文】group → 该 group 即将吸纳的 entity 列表。
		TMap<FArchetypeGroupHandle, TArray<FMassEntityHandle>> Groups;
	};

} // namespace UE::Mass

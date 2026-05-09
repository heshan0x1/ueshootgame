// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassEntityUtils.h"
#include "MassEntityTypes.h"
#include "MassArchetypeTypes.h"
#include "MassEntityManager.h"
#include "MassEntitySubsystem.h"
#include "Engine/EngineBaseTypes.h"
#include "Engine/World.h"
#if WITH_EDITOR
#include "Editor.h"
#endif // WITH_EDITOR

namespace UE::Mass::Utils
{
// 中文：把 World 的 NetMode 翻译成 Mass 的 EProcessorExecutionFlags。
//   - WITH_EDITOR 分支：仅当世界是"编辑器世界且非游戏世界"时返回 EditorWorld，
//     这覆盖编辑器内的预览/资源世界等场景，避免它们被当成 Standalone 跑游戏 processor。
//   - 之后按 NetMode 一一映射；ListenServer 同时具备 Client + Server 角色。
//   - default 分支用 checkf 显式 fail——遇到未支持的 NetMode 是配置错误。
EProcessorExecutionFlags GetProcessorExecutionFlagsForWorld(const UWorld& World)
{
#if WITH_EDITOR
	if (World.IsEditorWorld() && !World.IsGameWorld())
	{
		return EProcessorExecutionFlags::EditorWorld;
	}
#endif // WITH_EDITOR

	switch (const ENetMode NetMode = World.GetNetMode())
	{
	case NM_ListenServer:
		// 中文：监听服务器既要跑 Server 端逻辑，也要跑 Client 端（自己同时是玩家）。
		return EProcessorExecutionFlags::Client | EProcessorExecutionFlags::Server;
	case NM_DedicatedServer:
		return EProcessorExecutionFlags::Server;
	case NM_Client:
		return EProcessorExecutionFlags::Client;
	case NM_Standalone:
		return EProcessorExecutionFlags::Standalone;
	default:
		checkf(false, TEXT("Unsupported ENetMode type (%i) found while determining MASS processor execution flags."), NetMode);
		return EProcessorExecutionFlags::None;
	}
}

// 中文：execution flags 决策函数——按优先级返回。
//   1) 如果调用方传了非 None 的 override，就尊重 override（用于测试/工具/外部覆盖）；
//   2) World 非空就走基于 NetMode 的常规推导；
//   3) 没有 World 但处于编辑器（GEditor 存在）-> Editor；
//   4) 都没有：返回 All（最宽松，commandlet/CLI 等使用）。
EProcessorExecutionFlags DetermineProcessorExecutionFlags(const UWorld* World, EProcessorExecutionFlags ExecutionFlagsOverride)
{
	if (ExecutionFlagsOverride != EProcessorExecutionFlags::None)
	{
		return ExecutionFlagsOverride;
	}
	if (World)
	{
		return GetProcessorExecutionFlagsForWorld(*World);
	}

#if WITH_EDITOR
	if (GEditor)
	{
		return EProcessorExecutionFlags::Editor;
	}
#endif // WITH_EDITOR
	return EProcessorExecutionFlags::All;
}

// 中文：决定 processor phase 支持的 LEVELTICK_* 集合（bitfield）。
//   - 编辑器世界：返回 MAX_uint8 表示全开（编辑器场景下不知道会被怎么 tick，宽松一点）；
//   - 否则：返回标准游戏 tick + 暂停时的"仅时间 tick"。
uint8 DetermineProcessorSupportedTickTypes(const UWorld* World)
{
#if WITH_EDITOR
	if (World != nullptr && GetProcessorExecutionFlagsForWorld(*World) == EProcessorExecutionFlags::EditorWorld)
	{
		return MAX_uint8;
	}
#endif // WITH_EDITOR
	return (1 << LEVELTICK_All) | (1 << LEVELTICK_TimeOnly);
}

// 中文：把任意 entity handle 数组按 archetype 分桶，产出 FMassArchetypeEntityCollection 数组。
//   实现：
//     1) 用 TMap<archetype, TArray<handle>> 做 O(N) 分桶；
//     2) 跳过 invalid entity（IsEntityValid 校验）——避免把已销毁/未注册 handle 混入；
//     3) GetArchetypeForEntityUnsafe 在 IsEntityValid 通过后是安全的，且省掉 checked 路径的额外验证；
//     4) 每个桶产出一个 FMassArchetypeEntityCollection；构造时按 DuplicatesHandling 决定是否去重。
//   性能：DuplicatesHandling==NoDuplicates 时跳过去重排序；调用方需自行保证无重复。
void CreateEntityCollections(const FMassEntityManager& EntityManager, const TConstArrayView<FMassEntityHandle> Entities
	, const FMassArchetypeEntityCollection::EDuplicatesHandling DuplicatesHandling, TArray<FMassArchetypeEntityCollection>& OutEntityCollections)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR("Mass_CreateSparseChunks");

	// 中文：以 archetype 为 key 建临时映射；同一 archetype 的 handle 收到一起。
	TMap<const FMassArchetypeHandle, TArray<FMassEntityHandle>> ArchetypeToEntities;

	for (const FMassEntityHandle& Entity : Entities)
	{
		if (EntityManager.IsEntityValid(Entity))
		{
			FMassArchetypeHandle Archetype = EntityManager.GetArchetypeForEntityUnsafe(Entity);
			TArray<FMassEntityHandle>& PerArchetypeEntities = ArchetypeToEntities.FindOrAdd(Archetype);
			PerArchetypeEntities.Add(Entity);
		}
	}

	// 中文：把每个桶变成一个 collection。FMassArchetypeEntityCollection 的构造里，
	//   FoldDuplicates 模式会做一次排序+去重；NoDuplicates 模式仅做最少必要的整理。
	for (auto& Pair : ArchetypeToEntities)
	{
		OutEntityCollections.Add(FMassArchetypeEntityCollection(Pair.Key, Pair.Value, DuplicatesHandling));
	}
}

// 中文：从任意 UObject 拿默认 EntityManager。WorldContextObject 为空直接返回 nullptr。
FMassEntityManager* GetEntityManager(const UObject* WorldContextObject)
{
	return WorldContextObject
		? GetEntityManager(WorldContextObject->GetWorld())
		: nullptr;
}

// 中文：从 UWorld 拿默认 EntityManager。
//   走 UMassEntitySubsystem -> GetMutableEntityManager()；
//   subsystem 不存在（World 为空 or world 还没初始化好 / Mass 模块未启用）则返回 nullptr。
//   注意：当项目中存在多个 EntityManager 时（自建实例、独立 subsystem），此函数仅返回"subsystem 默认那个"。
FMassEntityManager* GetEntityManager(const UWorld* World)
{
	UMassEntitySubsystem* EntityManager = UWorld::GetSubsystem<UMassEntitySubsystem>(World);
	return EntityManager
		? &EntityManager->GetMutableEntityManager()
		: nullptr;
}

// 中文：Checked 版本——必须能拿到 subsystem，否则 check fail。返回引用让调用方写起来更简单。
FMassEntityManager& GetEntityManagerChecked(const UWorld& World)
{
	UMassEntitySubsystem* EntityManager = UWorld::GetSubsystem<UMassEntitySubsystem>(&World);
	check(EntityManager);
	return EntityManager->GetMutableEntityManager();
}

} // namespace UE::Mass::Utils
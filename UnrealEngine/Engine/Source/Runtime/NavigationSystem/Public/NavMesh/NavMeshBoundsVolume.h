// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * this volume only blocks the path builder - it has no gameplay collision
 *
 */
// ============================================================================
// 文件概览：NavMeshBoundsVolume.h
// ----------------------------------------------------------------------------
// 定义 ANavMeshBoundsVolume —— 关卡里"画一个框"来圈定导航网格生成范围。
// 该 Volume 对游戏物理没有任何碰撞作用，只影响 NavMesh 生成器：
//   - 每个 Volume 的 AABB 会被 UNavigationSystemV1 累积进 RegisteredNavBounds，
//     NavMesh 生成阶段只会在这些 Bounds 内（以 Tile 为粒度）建立导航数据。
//   - SupportedAgents 支持按 Agent 过滤，让不同代理共用/独用不同的边界盒。
// 关联文件：NavigationSystem.h/.cpp 中 GatherNavigationBounds / OnNavigationBoundsUpdated
// 生命周期：PostRegisterAllComponents → 通知 NavSys 新增一块边界；
//           PostUnregisterAllComponents → 通知 NavSys 移除；
//           PostEditChangeProperty / PostEditUndo → 编辑器中实时更新。
// ============================================================================

#pragma once

#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_4
#include "CoreMinimal.h"
#endif
#include "UObject/ObjectMacros.h"
#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_4
#include "AI/Navigation/NavigationTypes.h"
#endif
#include "GameFramework/Volume.h"
#include "AI/Navigation/NavAgentSelector.h"
#include "NavMeshBoundsVolume.generated.h"


UCLASS(MinimalAPI)
class ANavMeshBoundsVolume : public AVolume
{
	GENERATED_UCLASS_BODY()

	// 允许哪些 Agent（代理类型）使用本边界盒生成导航网格。
	// 默认勾选全部；可在编辑器中按 Agent 过滤，让 Agent-A 与 Agent-B 共享一套关卡
	// 但使用不同的导航覆盖范围。
	UPROPERTY(EditAnywhere, Category = Navigation)
	FNavAgentSelector SupportedAgents;

	//~ Begin AActor Interface
	// Volume 注册完成时：将自身 AABB 告知 UNavigationSystemV1，触发相应 Tile 的生成/激活
	NAVIGATIONSYSTEM_API virtual void PostRegisterAllComponents() override;
	// Volume 反注册时：从 NavSys 的 RegisteredNavBounds 中移除并可能丢弃对应 Tile
	NAVIGATIONSYSTEM_API virtual void PostUnregisterAllComponents() override;
	//~ End AActor Interface
#if WITH_EDITOR
	//~ Begin UObject Interface
	// 编辑器中改动属性（例如缩放/SupportedAgents）时：重新推送 Bounds 通知
	NAVIGATIONSYSTEM_API virtual void PostEditChangeProperty( struct FPropertyChangedEvent& PropertyChangedEvent) override;
	// Undo/Redo 后同样需要刷新 Bounds 通知
	NAVIGATIONSYSTEM_API virtual void PostEditUndo() override;
	//~ End UObject Interface

	// 引擎后初始化：在编辑器模式挂接全局委托，确保 Volume 从磁盘加载后能被正确感知。
	// 由 FNavigationSystemModule::StartupModule 注册。
	static NAVIGATIONSYSTEM_API void OnPostEngineInit();
#endif // WITH_EDITOR
};


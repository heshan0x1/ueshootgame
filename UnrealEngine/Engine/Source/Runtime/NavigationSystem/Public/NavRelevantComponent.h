// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// NavRelevantComponent.h
// -----------------------------------------------------------------------------
// 所有"挂到 Actor 上参与导航生成"的组件的基类。
// 实现了 INavRelevantInterface，于是可以被 UNavigationSystemV1 收录到
// FNavigationOctree 里。派生类：UNavModifierComponent / UNavLinkComponent /
// UNavLinkCustomComponent / USplineNavModifierComponent 等。
//
// 组件状态流转（见架构文档 4.2 节）：
//   OnRegister → RegisterComponentWithNavigationSystem → 进入 Octree PendingUpdates
//   Tick: ProcessPendingOctreeUpdates → 真正入 Octree + 打 Dirty Area
//   OnUnregister → 反向移除
//
// 主要字段：
//   Bounds —— 在 Octree 里占用的 AABB（世界空间），由 UpdateNavigationBounds
//             委托 CalcAndCacheBounds 计算
//   bAttachToOwnersRoot —— true 时此组件不单独占槽，复用 Owner 根组件的 Octree 项
// =============================================================================

#pragma once

#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_4
#include "CoreMinimal.h"
#endif
#include "UObject/ObjectMacros.h"
#include "Components/ActorComponent.h"
#include "AI/Navigation/NavRelevantInterface.h"
#include "NavRelevantComponent.generated.h"

// 可参与导航生成的 ActorComponent 基类。实现 INavRelevantInterface 暴露 Bounds 与几何贡献。
UCLASS(MinimalAPI)
class UNavRelevantComponent : public UActorComponent, public INavRelevantInterface
{
	GENERATED_UCLASS_BODY()

	//~ Begin UActorComponent Interface
	// 组件注册时：自动登记到所在 World 的 NavigationSystem（Octree）
	NAVIGATIONSYSTEM_API virtual void OnRegister() override;
	// 组件注销时：从 Octree 移除
	NAVIGATIONSYSTEM_API virtual void OnUnregister() override;
	//~ End UActorComponent Interface

	//~ Begin INavRelevantInterface Interface
	// 返回缓存的 Bounds（若未初始化会触发一次 CalcAndCacheBounds）
	NAVIGATIONSYSTEM_API virtual FBox GetNavigationBounds() const override;
	// 是否参与导航：默认 bIsActive && IsRegistered
	NAVIGATIONSYSTEM_API virtual bool IsNavigationRelevant() const override;
	// 强制触发一次 Bounds 重算 + 通知 NavigationSystem 更新 Octree 项
	NAVIGATIONSYSTEM_API virtual void UpdateNavigationBounds() override;
	// 返回导航意义上的"父对象"——当 bAttachToOwnersRoot 为 true 时返回 Owner 根组件，
	// 让多个 Modifier 聚合到同一 Octree 项，避免重复登记。
	NAVIGATIONSYSTEM_API virtual UObject* GetNavigationParent() const override;
	//~ End INavRelevantInterface Interface
	
	// 子类重写：计算并缓存自己的 Bounds（AABB）。默认实现为空。
	NAVIGATIONSYSTEM_API virtual void CalcAndCacheBounds() const;

	// 蓝图可用：在运行时切换"是否参与导航"。
	UFUNCTION(BlueprintCallable, Category="AI|Navigation")
	NAVIGATIONSYSTEM_API void SetNavigationRelevancy(bool bRelevant);

	/** force relevancy and skip attaching navigation data to owner's root entry */
	// 强制相关性：设置后此组件独立登记，不再作为 Owner 根组件的"附加信息"。
	NAVIGATIONSYSTEM_API void ForceNavigationRelevancy(bool bForce);

	/** force refresh in navigation octree */
	// 强制通知 NavigationSystem 重新拉取本组件的数据（用于 Modifier/Link 参数改变后）
	NAVIGATIONSYSTEM_API void RefreshNavigationModifiers();
	
protected:

	/** bounds for navigation octree */
	// Octree AABB 缓存；mutable 因 Getter 里懒计算；由 CalcAndCacheBounds 填充。
	mutable FBox Bounds;
	
	/** attach navigation data to entry for owner's root component (depends on its relevancy) */
	// true → 本组件不单独登记到 Octree，其数据挂到 Owner 根组件的 Octree Entry 下
	UPROPERTY()
	uint32 bAttachToOwnersRoot : 1;

	// Bounds 是否已初始化，用于惰性计算
	mutable uint32 bBoundsInitialized : 1;
	// CachedNavParent 是否已计算
	uint32 bNavParentCacheInitialized : 1;

	// GetNavigationParent 的缓存结果（避免每次查询都向上遍历）
	UPROPERTY(transient)
	TObjectPtr<UObject> CachedNavParent;
};

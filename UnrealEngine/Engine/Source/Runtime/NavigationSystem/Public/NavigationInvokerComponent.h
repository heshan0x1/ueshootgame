// Copyright Epic Games, Inc. All Rights Reserved.
// =============================================================================
// NavigationInvokerComponent.h
// -----------------------------------------------------------------------------
// Navigation Invoker（导航召唤者）组件。
// 只有 `bGenerateNavigationOnlyAroundNavigationInvokers=true` 时才真正生效。
// 作用：
//   Actor 身上挂此组件 → 组件 Activate 时 RegisterWithNavigationSystem →
//   进入 UNavigationSystemV1::Invokers Map →
//   每 ActiveTilesUpdateInterval 秒系统会读取 (Owner 位置, GenerationRadius,
//   RemovalRadius, Priority, SupportedAgents)，驱动本地 Tile 生成 / 丢弃。
// 典型用途：在开放世界里只在玩家附近生成导航网格，节省内存。
// 见架构文档 4.6 节。
// =============================================================================
#pragma once

#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_4
#include "CoreMinimal.h"
#endif
#include "UObject/ObjectMacros.h"
#include "UObject/UObjectGlobals.h"
#include "AI/Navigation/NavAgentSelector.h"
#include "AI/Navigation/NavigationTypes.h"
#include "Components/ActorComponent.h"
#include "NavigationInvokerComponent.generated.h"

class UNavigationSystemV1;
enum class ENavigationInvokerPriority : uint8;

// 导航召唤者组件：挂到 Actor（通常是玩家 Pawn）身上，通知导航系统"在我附近生成 NavMesh"。
UCLASS(ClassGroup = (Navigation), meta = (BlueprintSpawnableComponent), MinimalAPI)
class UNavigationInvokerComponent : public UActorComponent
{
	GENERATED_BODY()

protected:

	/** Navigation data is requested within a TileGenerationRadius circle around the component owner. */
	// 生成半径：以 Owner 位置为中心，此半径内的 Tile 会被加入活跃集、生成导航数据。
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Navigation, meta = (ClampMin = "0.1", ClampMax = "6400000", UIMin = "0.1", UIMax = "6400000"))
	float TileGenerationRadius;

	/** Navigation data can be discarded when outside of a TileRemovalRadius circle around the component owner.
	 * This is computed for all navigation invokers, so a navigation data must be outside of all navigation invokers TileRemovalRadius circles to be discarded. */
	// 移除半径：超过此半径的 Tile 有资格被丢弃（注意：必须对 *全部* Invoker 都超出才会真正释放）。
	// 通常 RemovalRadius > GenerationRadius，形成一个"滞后带"，避免玩家反复穿越边界时频繁重建。
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Navigation, meta = (ClampMin = "0.1", ClampMax = "6400000", UIMin = "0.1", UIMax = "6400000"))
	float TileRemovalRadius;

	/** restrict navigation generation to specific agents */
	// 指定该 Invoker 只对某些代理类型生效（位掩码）。例如只对小型 AI 代理激活。
	UPROPERTY(EditAnywhere, Category = Navigation)
	FNavAgentSelector SupportedAgents;

	/** Experimental invocation priority. It will modify the order in which invoked tiles are being built if SortPendingTilesMethod is set to SortByPriority. */
	// 优先级：多个 Invoker 同时激活时，用于 PendingTiles 排序（仅在 SortByPriority 模式下生效）。
	UPROPERTY(EditAnywhere, Category = Navigation)
	ENavigationInvokerPriority Priority;

public:
	NAVIGATIONSYSTEM_API UNavigationInvokerComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	// 将自身注册到传入的 NavigationSystem 中。
	// 线程：GameThread；调用者：Activate 或 OnNavigationInitStart 回调。
	NAVIGATIONSYSTEM_API void RegisterWithNavigationSystem(UNavigationSystemV1& NavSys);

	/** Sets generation/removal ranges. Doesn't force navigation system's update.
	 *	Will get picked up the next time NavigationSystem::UpdateInvokers gets called */
	// 运行时修改半径。注意：不会立即触发导航系统更新，下次 UpdateInvokers 周期才会生效。
	NAVIGATIONSYSTEM_API void SetGenerationRadii(const float GenerationRadius, const float RemovalRadius);

	// 只读访问当前半径
	float GetGenerationRadius() const { return TileGenerationRadius; }
	float GetRemovalRadius() const { return TileRemovalRadius; }

	// Activate：把自己登记进 NavigationSystem 的 Invokers Map
	NAVIGATIONSYSTEM_API virtual void Activate(bool bReset = false) override;
	// Deactivate：从 Invokers Map 中移除
	NAVIGATIONSYSTEM_API virtual void Deactivate() override;
	// PostInitProperties：若 NavigationSystem 尚未初始化，挂 OnNavigationInitStart 回调等待
	NAVIGATIONSYSTEM_API virtual void PostInitProperties() override;

private:
	// NavigationSystem 初始化开始时的回调——此时再把自己注册上去。
	void OnNavigationInitStart(const class UNavigationSystemBase& NavSys);
};

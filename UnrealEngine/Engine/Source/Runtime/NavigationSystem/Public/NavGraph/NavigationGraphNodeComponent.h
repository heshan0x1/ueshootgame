// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// NavigationGraphNodeComponent.h
// -----------------------------------------------------------------------------
// SceneComponent 形式的图节点。用双向链表 (Prev/Next) 串联，
// BeginDestroy 里会自动从链表中移除自身、维持前后邻居的连接。
// 典型用法：Actor 上挂若干 UNavigationGraphNodeComponent，形成可寻路节点链。
// NavGraph 目前整体处于半成品阶段，此组件主要作为节点容器存在。
// =============================================================================

#pragma once

#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_4
#include "CoreMinimal.h"
#endif
#include "UObject/ObjectMacros.h"
#include "Components/SceneComponent.h"
#include "NavGraph/NavigationGraph.h"
#include "NavigationGraphNodeComponent.generated.h"

// 图导航节点组件：以 SceneComponent 形式存在，持有一个 FNavGraphNode 数据 + 前后指针。
UCLASS(config=Engine, MinimalAPI, HideCategories=(Mobility))
class UNavigationGraphNodeComponent : public USceneComponent
{
	GENERATED_UCLASS_BODY()

	// 该组件承载的图节点数据（Owner、出边等）
	UPROPERTY()
	FNavGraphNode Node;

	// 双向链表下一节点（同一 Actor 内或跨 Actor 均可）
	UPROPERTY()
	TObjectPtr<UNavigationGraphNodeComponent> NextNodeComponent;

	// 双向链表上一节点
	UPROPERTY()
	TObjectPtr<UNavigationGraphNodeComponent> PrevNodeComponent;

	//~ Begin UObject Interface.
	// 销毁时自动"脱链"——把前后邻居相互连接，避免悬空指针。
	virtual void BeginDestroy() override;
};

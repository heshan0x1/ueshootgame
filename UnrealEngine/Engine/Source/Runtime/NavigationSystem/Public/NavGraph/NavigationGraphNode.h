// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// NavigationGraphNode.h
// -----------------------------------------------------------------------------
// "图式导航数据" 的节点 Actor 基类。当前为 abstract 占位，
// 真正挂到世界里的节点通常通过 UNavigationGraphNodeComponent 实现。
// 见架构文档 Layer 4' 说明——NavGraph 整体为半成品。
// =============================================================================

#pragma once

#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_4
#include "CoreMinimal.h"
#endif
#include "UObject/ObjectMacros.h"
#include "GameFramework/Actor.h"

#include "NavigationGraphNode.generated.h"

// 图式导航的节点 Actor 基类；NotBlueprintable + abstract，目前不能直接使用。
UCLASS(config=Engine, MinimalAPI, NotBlueprintable, abstract)
class ANavigationGraphNode : public AActor
{
	GENERATED_UCLASS_BODY()
};

// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// 文件概览：
//   定义 INavNodeInterface —— Actor 若希望暴露自身所持有的
//   UNavigationGraphNodeComponent（NavigationGraph 的一个节点），
//   应实现本接口。这是 NavGraph（区别于 NavMesh 的导航图实现）侧用来
//   发现节点的统一钩子。
//
// 与其它文件的关系：
//   - 实现文件：Private/NavigationInterfaces.cpp（仅提供构造体）。
//   - 使用者：UNavigationGraphNodeComponent / ANavigationGraph 查询 Actor 节点。
// =============================================================================

#pragma once

#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_4
#include "CoreMinimal.h"
#endif
#include "UObject/ObjectMacros.h"
#include "UObject/Interface.h"
#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_4
#include "AI/Navigation/NavigationTypes.h"
#endif
#include "NavNodeInterface.generated.h"

// UInterface 壳：反射系统用，不在蓝图中实现。
UINTERFACE(MinimalAPI, meta=(CannotImplementInterfaceInBlueprint))
class UNavNodeInterface : public UInterface
{
	GENERATED_UINTERFACE_BODY()
};

// 实际接口体：实现者需要能返回自身的 NavigationGraph 节点组件。
class INavNodeInterface
{
	GENERATED_IINTERFACE_BODY()


	/**
	 *	Retrieves pointer to implementation's UNavigationGraphNodeComponent
	 */
	// 纯虚：返回实现者持有的 NavGraph 节点组件；无节点时应返回 nullptr。
	virtual class UNavigationGraphNodeComponent* GetNavNodeComponent() PURE_VIRTUAL(FNavAgentProperties::GetNavNodeComponent,return NULL;);

};

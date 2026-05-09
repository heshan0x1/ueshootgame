// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// 文件概览：
//   定义 INavigationPathGenerator —— 一个极窄的接口，任何"能为给定
//   NavAgent 生产 FNavigationPath"的对象（例如 AIController 或自定义路径源）
//   可以实现它以统一对外暴露"查询最近一次生成路径"的能力。
//
// 与其它文件的关系：
//   - 实现文件：Private/NavigationSystemTypes.cpp 提供 UObject 构造体。
//   - 主要消费者为 AI/DebugTools 代码，用于可视化某 Agent 当前的路径。
// =============================================================================

#pragma once

#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_4
#include "CoreMinimal.h"
#endif
#include "UObject/ObjectMacros.h"
#include "UObject/Interface.h"
#include "AI/Navigation/NavigationTypes.h"
#include "NavigationPathGenerator.generated.h"

// UInterface 壳：反射/CDO 用，不暴露给蓝图实现。
UINTERFACE(MinimalAPI, meta=(CannotImplementInterfaceInBlueprint))
class UNavigationPathGenerator : public UInterface
{
	GENERATED_UINTERFACE_BODY()
};

class INavigationPathGenerator
{
	GENERATED_IINTERFACE_BODY()


	/** 
	 *	Retrieved path generated for specified navigation Agent
	 */
	// 纯虚：返回为指定 Agent 已经生成的路径共享指针；未生成则返回空。
	virtual FNavPathSharedPtr GetGeneratedPath(class INavAgentInterface* Agent) PURE_VIRTUAL(INavigationPathGenerator::GetGeneratedPath, return FNavPathSharedPtr(NULL););
};

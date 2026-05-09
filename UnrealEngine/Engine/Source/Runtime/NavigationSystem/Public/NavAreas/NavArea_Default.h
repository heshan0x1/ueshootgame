// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// 文件概览：
//   UNavArea_Default —— 默认可行走 Area。NavMesh 生成时整块网格默认使用
//   该 Area 类，除非被 Modifier/NavLink 显式覆盖。
//   Cost=1，AreaFlags=1（可通行），DrawColor=Magenta（来自基类默认值）。
// =============================================================================

#pragma once

#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_4
#include "CoreMinimal.h"
#endif
#include "NavAreas/NavArea.h"
#include "UObject/ObjectMacros.h"
#include "UObject/UObjectGlobals.h"

#include "NavArea_Default.generated.h"

class UObject;

/** Regular navigation area, applied to entire navigation data by default */
// 默认可行走区，NavMesh 整体默认属性。无自定义字段。
UCLASS(Config=Engine, MinimalAPI)
class UNavArea_Default : public UNavArea
{
	GENERATED_UCLASS_BODY()
};

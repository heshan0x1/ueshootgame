// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// 文件概览：
//   UNavArea_Null —— 空区，任何人都不可通行。
//   Cost=FLT_MAX，AreaFlags=0（Recast 中 Flag=0 等价于"Unwalkable"）。
//   常用于手动屏蔽某一块区域，或被 Modifier 覆盖后剔除掉某些 Poly。
// =============================================================================

#pragma once

#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_4
#include "CoreMinimal.h"
#endif
#include "NavAreas/NavArea.h"
#include "UObject/ObjectMacros.h"
#include "UObject/UObjectGlobals.h"

#include "NavArea_Null.generated.h"

class UObject;

/** In general represents an empty area, that cannot be traversed by anyone. Ever.*/
// 空区：绝对不可通行。
UCLASS(Config=Engine, MinimalAPI)
class UNavArea_Null : public UNavArea
{
	GENERATED_UCLASS_BODY()
};

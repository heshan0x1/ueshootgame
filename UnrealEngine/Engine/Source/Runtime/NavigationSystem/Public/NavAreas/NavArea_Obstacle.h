// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// 文件概览：
//   UNavArea_Obstacle —— 高代价障碍区。
//   Cost=1,000,000，AreaFlags 继承基类默认值 1（仍可通行，但极不划算）。
//   典型用途：Agent 只在完全没有其他路径时才会选择穿过（例如地雷区/灼热地面）。
// =============================================================================

#pragma once

#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_4
#include "CoreMinimal.h"
#endif
#include "NavAreas/NavArea.h"
#include "UObject/ObjectMacros.h"
#include "UObject/UObjectGlobals.h"

#include "NavArea_Obstacle.generated.h"

class UObject;

/** In general represents a high cost area, that shouldn't be traversed by anyone unless no other path exist.*/
// 障碍区：仍可通行但代价极高（1e6），寻路器会优先绕开。
UCLASS(Config = Engine, MinimalAPI)
class UNavArea_Obstacle : public UNavArea
{
	GENERATED_UCLASS_BODY()
};

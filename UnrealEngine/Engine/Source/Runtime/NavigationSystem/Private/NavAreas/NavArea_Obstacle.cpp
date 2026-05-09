// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// 文件概览：
//   UNavArea_Obstacle 的实现。
//   Cost=1e6，保留基类 AreaFlags=1（可通行）——寻路会尽量绕开，但不会完全排除。
//   棕色调便于在 NavMesh 调试视图里识别为"障碍区"。
// =============================================================================

#include "NavAreas/NavArea_Obstacle.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(NavArea_Obstacle)

// 构造：棕色 + 百万级代价。
UNavArea_Obstacle::UNavArea_Obstacle(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	DrawColor = FColor(127, 51, 0);	// brownish
	DefaultCost = 1000000.0f;
}


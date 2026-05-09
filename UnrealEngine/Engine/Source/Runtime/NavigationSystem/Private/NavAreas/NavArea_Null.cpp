// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// 文件概览：
//   UNavArea_Null 的实现。
//   Cost=FLT_MAX，AreaFlags=0（Unwalkable）——任何 Agent 绝对不可通行。
// =============================================================================

#include "NavAreas/NavArea_Null.h"
#include "NavMesh/RecastNavMesh.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(NavArea_Null)

// 构造：最大代价 + Unwalkable 标志位。
UNavArea_Null::UNavArea_Null(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	DefaultCost = FLT_MAX;
	AreaFlags = 0;
}


// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// 文件概览：
//   UNavArea_LowHeight 的实现。
//   Cost=BIG_NUMBER，AreaFlags=0 表示绝不允许通行；深蓝色以便在 NavMesh 调试视图区分。
//   配合 IsLowArea()==true 让 Recast 过滤器识别为"低顶棚区"。
// =============================================================================

#include "NavAreas/NavArea_LowHeight.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(NavArea_LowHeight)

// 构造：极高代价 + 不可通行标志位 + 深蓝色。
UNavArea_LowHeight::UNavArea_LowHeight(const FObjectInitializer& ObjectInitializer) 
	: Super(ObjectInitializer)
{
	DefaultCost = BIG_NUMBER;
	DrawColor = FColor(0, 0, 128);

	// can't traverse
	// AreaFlags=0 等价于 Recast "Unwalkable"。
	AreaFlags = 0;
}


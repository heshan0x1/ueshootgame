// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// 文件概览：
//   UNavArea_Default 的实现。仅调用基类构造，继承 Cost=1 / AreaFlags=1（可通行）。
//   作为 NavMesh 的默认可行走 Area。
// =============================================================================

#include "NavAreas/NavArea_Default.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(NavArea_Default)

// 默认构造：完全沿用基类数值（Cost=1，Flag=1，粉色）。
UNavArea_Default::UNavArea_Default(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
}


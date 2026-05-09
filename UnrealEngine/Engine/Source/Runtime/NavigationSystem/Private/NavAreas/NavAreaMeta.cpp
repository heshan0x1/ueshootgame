// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// 文件概览：
//   UNavAreaMeta 的实现——仅构造时把 bIsMetaArea 置 true，
//   其余行为全部继承自 UNavArea。
// =============================================================================

#include "NavAreas/NavAreaMeta.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(NavAreaMeta)

// 构造：把基类的 bIsMetaArea 打开——让 IsMetaArea() 返回 true，
// 以便 Recast 生成阶段识别本类需要"按 Agent 挑选最终 Area 类"的流程。
UNavAreaMeta::UNavAreaMeta(const FObjectInitializer& ObjectInitializer) 
	: Super(ObjectInitializer)
{
	bIsMetaArea = true;
}


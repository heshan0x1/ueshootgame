// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// 文件概览：
//   定义 UNavAreaMeta —— Meta 区域（元区域）抽象基类。
//   "Meta" 意味着这个 Area 不会被直接写到 NavMesh 里，而是在 Tile 生成时
//   按 Agent / 运行时条件选择一个具体 UNavArea 派生类作为替代品。
//
//   典型派生：UNavAreaMeta_SwitchByAgent —— 按 AgentIndex 挑选。
//
// 注意：
//   检查"某 Area 是否为 Meta"请用 IsMetaArea()，而不是 IsA<UNavAreaMeta>()
//   —— 用户可以派生出自己的 Meta Area 而不继承本类。
// =============================================================================

#pragma once

#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_4
#include "CoreMinimal.h"
#endif
#include "NavAreas/NavArea.h"
#include "Templates/SubclassOf.h"
#include "UObject/ObjectMacros.h"
#include "UObject/UObjectGlobals.h"

#include "NavAreaMeta.generated.h"

class AActor;
class UObject;

/** A convenience class for an area that has IsMetaArea() == true.
 *	Do not use this class when determining whether an area class is "meta". 
 *	Call IsMetaArea instead. */
// Meta Area 便捷基类：构造时直接把 bIsMetaArea 标记为 true。
UCLASS(Abstract, MinimalAPI)
class UNavAreaMeta : public UNavArea
{
	GENERATED_BODY()

public:
	// 构造：仅设置 bIsMetaArea=true，不改变 Cost/Flag。
	NAVIGATIONSYSTEM_API UNavAreaMeta(const FObjectInitializer& ObjectInitializer);
};

// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// 文件概览：
//   UNavLinkTrivial 是 UNavLinkDefinition 的最简派生类，作为"占位/测试用"
//   的链接定义——不添加任何自定义逻辑，主要出现在默认配置与示例蓝图里。
//   真正生产环境的自定义链接请使用 UNavLinkCustomComponent / 自定义
//   UNavLinkDefinition 派生类。
// =============================================================================

#pragma once

#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_4
#include "CoreMinimal.h"
#endif
#include "UObject/ObjectMacros.h"
#include "AI/Navigation/NavLinkDefinition.h"
#include "NavLinkTrivial.generated.h"

// 最小化的 NavLinkDefinition 子类；除基类行为外无任何额外逻辑。
UCLASS(MinimalAPI)
class UNavLinkTrivial : public UNavLinkDefinition
{
	GENERATED_UCLASS_BODY()
};

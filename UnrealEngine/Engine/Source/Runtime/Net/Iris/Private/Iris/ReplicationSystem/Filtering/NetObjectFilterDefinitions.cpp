// Copyright Epic Games, Inc. All Rights Reserved.

// =====================================================================================================================
// NetObjectFilterDefinitions.cpp —— UNetObjectFilterDefinitions 的极简实现。
// 仅暴露一个 getter；ini 反射加载完全由 UPROPERTY(Config) + UCLASS(config=Engine) 自动完成。
// =====================================================================================================================

#include "Iris/ReplicationSystem/Filtering/NetObjectFilterDefinitions.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(NetObjectFilterDefinitions)

// 直接以视图形式暴露内部数组（零拷贝）。
TConstArrayView<FNetObjectFilterDefinition> UNetObjectFilterDefinitions::GetFilterDefinitions() const
{
	return MakeArrayView(NetObjectFilterDefinitions);
}

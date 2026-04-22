// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tasks/SimpleTargetingSortTask.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(SimpleTargetingSortTask)

float USimpleTargetingSortTask::GetScoreForTarget(const FTargetingRequestHandle& TargetingHandle, const FTargetingDefaultResultData& TargetData) const
{
	return BP_GetScoreForTarget(TargetingHandle, TargetData);
}

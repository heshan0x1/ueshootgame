// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tasks/SimpleTargetingFilterTask.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(SimpleTargetingFilterTask)

bool USimpleTargetingFilterTask::ShouldFilterTarget(const FTargetingRequestHandle& TargetingHandle, const FTargetingDefaultResultData& TargetData) const
{
	return BP_ShouldFilterTarget(TargetingHandle, TargetData);
}

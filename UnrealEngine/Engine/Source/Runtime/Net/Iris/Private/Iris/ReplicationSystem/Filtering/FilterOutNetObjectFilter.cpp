// Copyright Epic Games, Inc. All Rights Reserved.

#include "Iris/ReplicationSystem/Filtering/FilterOutNetObjectFilter.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(FilterOutNetObjectFilter)

void UFilterOutNetObjectFilter::Filter(FNetObjectFilteringParams& Params)
{
	// Filter out everything
	Params.OutAllowedObjects.ClearAllBits();
}

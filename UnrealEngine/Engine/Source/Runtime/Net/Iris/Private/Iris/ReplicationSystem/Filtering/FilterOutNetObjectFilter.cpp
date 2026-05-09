// Copyright Epic Games, Inc. All Rights Reserved.

// =====================================================================================================================
// FilterOutNetObjectFilter.cpp —— UFilterOutNetObjectFilter 的实现。
// Filter() 把 OutAllowedObjects 整体清零，等效"对所有连接、所有归属对象一律 Disallow"。
// 注意：归属本 Filter 但又被 Inclusion Group 选中的对象会在 FReplicationFiltering::UpdateGroupInclusionFiltering()
// 中被恢复进入 Scope（详见 ReplicationFiltering.cpp 三阶段流程）。
// =====================================================================================================================

#include "Iris/ReplicationSystem/Filtering/FilterOutNetObjectFilter.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(FilterOutNetObjectFilter)

// 全部拒绝。Inclusion Group 会在更外层把"破例对象"重新置位。
void UFilterOutNetObjectFilter::Filter(FNetObjectFilteringParams& Params)
{
	// Filter out everything
	Params.OutAllowedObjects.ClearAllBits();
}

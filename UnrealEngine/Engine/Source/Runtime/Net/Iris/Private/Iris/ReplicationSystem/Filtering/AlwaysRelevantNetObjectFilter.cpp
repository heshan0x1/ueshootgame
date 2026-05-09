// Copyright Epic Games, Inc. All Rights Reserved.

// =====================================================================================================================
// AlwaysRelevantNetObjectFilter.cpp —— UAlwaysRelevantNetObjectFilter 的实现。
// 几乎所有钩子都是空函数，唯一真正干活的是 Filter()：把 OutAllowedObjects 整体置位（全部 Allow）。
// 调度器随后会把这个全 1 位图与 FilteredObjects 在外层取交集，等效于"本 Filter 接管的对象全部 Allow"。
// =====================================================================================================================

#include "Iris/ReplicationSystem/Filtering/AlwaysRelevantNetObjectFilter.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AlwaysRelevantNetObjectFilter)

// 不读 Config，无 per-connection 数据，空初始化即可。
void UAlwaysRelevantNetObjectFilter::OnInit(const FNetObjectFilterInitParams& Params)
{
}

// 无外部引用要释放。
void UAlwaysRelevantNetObjectFilter::OnDeinit()
{
}

// 接受所有添加请求，无私有数据需要写入 OutInfo。
bool UAlwaysRelevantNetObjectFilter::AddObject(uint32 ObjectIndex, FNetObjectFilterAddObjectParams& Params)
{
	return true;
}

// 无 per-object 状态，移除空实现。
void UAlwaysRelevantNetObjectFilter::RemoveObject(uint32 ObjectIndex, const FNetObjectFilteringInfo& Info)
{
}

// 未声明 NeedsUpdate trait，本函数不会被调度器调用，留空作占位。
void UAlwaysRelevantNetObjectFilter::UpdateObjects(FNetObjectFilterUpdateParams& Params)
{
}

// 主过滤逻辑：所有对象一律 Allow。利用位图的 SetAllBits 一次性完成（O(words)）。
void UAlwaysRelevantNetObjectFilter::Filter(FNetObjectFilteringParams& Params)
{
	// Allow everything
	Params.OutAllowedObjects.SetAllBits();
}

// 索引上限扩容时无需迁移内部数据。
void UAlwaysRelevantNetObjectFilter::OnMaxInternalNetRefIndexIncreased(uint32 NewMaxInternalIndex)
{
}


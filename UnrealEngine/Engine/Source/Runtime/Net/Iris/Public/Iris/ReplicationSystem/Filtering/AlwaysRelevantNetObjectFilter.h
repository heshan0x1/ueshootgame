// Copyright Epic Games, Inc. All Rights Reserved.

// =====================================================================================================================
// AlwaysRelevantNetObjectFilter.h —— "永远相关"过滤器：
//   对所有归属本 Filter 的对象，每个连接每帧都返回 Allow（OutAllowedObjects.SetAllBits()）。
//   语义等价于"不参与动态过滤"，但与"无 Filter"略有差别——AlwaysRelevant 的对象仍会被记入 DynamicFilterEnabledObjects
//   并因此被 BuildAlwaysRelevantList() 视为非"系统级 always-relevant"对象（参见 ReplicationFiltering.cpp）。
// 典型用途：游戏中需要"永远复制给所有人"的关键全局对象。
// =====================================================================================================================

#pragma once

#include "Iris/ReplicationSystem/Filtering/NetObjectFilter.h"
#include "AlwaysRelevantNetObjectFilter.generated.h"

/** AlwaysRelevant 的 Config（占位，无字段；按惯例必须存在以便 ini 注册）。 */
UCLASS(transient, MinimalAPI)
class UAlwaysRelevantNetObjectFilterConfig final : public UNetObjectFilterConfig
{
	GENERATED_BODY()
};

/**
 * UAlwaysRelevantNetObjectFilter —— 永远 Allow 的过滤器实现。
 * 不持有 per-connection / per-object 状态；Filter() 只做一次 SetAllBits。
 */
UCLASS()
class UAlwaysRelevantNetObjectFilter final : public UNetObjectFilter
{
	GENERATED_BODY()

protected:
	// UNetObjectFilter interface
	IRISCORE_API virtual void OnInit(const FNetObjectFilterInitParams&) override;            // 空实现
	IRISCORE_API virtual void OnDeinit() override;                                            // 空实现
	IRISCORE_API virtual bool AddObject(uint32 ObjectIndex, FNetObjectFilterAddObjectParams&) override; // 直接接受
	IRISCORE_API virtual void RemoveObject(uint32 ObjectIndex, const FNetObjectFilteringInfo&) override; // 空实现
	IRISCORE_API virtual void UpdateObjects(FNetObjectFilterUpdateParams&) override;          // 空实现
	IRISCORE_API virtual void Filter(FNetObjectFilteringParams&) override;                    // OutAllowedObjects.SetAllBits()
	IRISCORE_API virtual void OnMaxInternalNetRefIndexIncreased(uint32 NewMaxInternalIndex) override; // 空实现
};

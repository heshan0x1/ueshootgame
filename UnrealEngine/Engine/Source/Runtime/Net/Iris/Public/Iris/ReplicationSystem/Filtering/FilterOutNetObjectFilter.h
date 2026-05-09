// Copyright Epic Games, Inc. All Rights Reserved.

// =====================================================================================================================
// FilterOutNetObjectFilter.h —— "永远过滤掉"过滤器。
// 适用场景：游戏中默认不复制、需要"显式相关"才能进入 scope 的对象。配合上层 Inclusion 机制：
//   - UReplicationSystem::AddInclusionFilterGroup 把对象按需加入 InclusionGroup；
//   - UObjectReplicationBridge::AddDependentObject 通过依赖链相关；
//   - 高优先级 Relevancy 设置覆盖。
// 这种"默认拒绝 + 选择性允许"的模式比"默认允许 + 黑名单"更安全可控。
// =====================================================================================================================

#pragma once

#include "Iris/ReplicationSystem/Filtering/NetObjectFilter.h"
#include "FilterOutNetObjectFilter.generated.h"

/** FilterOut 的 Config（占位）。 */
UCLASS(transient, MinimalAPI)
class UFilterOutNetObjectFilterConfig final : public UNetObjectFilterConfig
{
	GENERATED_BODY()
};

/**
 * Objects added to this filter will never be relevant automatically.
 * In order to become relevant to a connection they need to be part of higher-priority relevancy settings.
 * Ex: @see UReplicationSystem::AddInclusionFilterGroup, UObjectReplicationBridge::AddDependentObject, etc.
 */
/**
 * UFilterOutNetObjectFilter —— 默认全拒绝。仅可通过更高优先级的 relevancy 设置（Inclusion Group / Dependent
 * Object 等）"破例"使对象进入连接 Scope。
 */
UCLASS()
class UFilterOutNetObjectFilter final : public UNetObjectFilter
{
	GENERATED_BODY()

protected:
	// UNetObjectFilter interface
	virtual void OnInit(const FNetObjectFilterInitParams&) override {}                          // 空实现
	virtual void OnDeinit() override {}                                                          // 空实现
	virtual void OnMaxInternalNetRefIndexIncreased(uint32 NewMaxInternalIndex) override {}       // 空实现
	virtual bool AddObject(uint32 ObjectIndex, FNetObjectFilterAddObjectParams&) override { return true; } // 直接接受
	virtual void RemoveObject(uint32 ObjectIndex, const FNetObjectFilteringInfo&) override {}    // 空实现

	/** 主过滤：调用 OutAllowedObjects.ClearAllBits() 全部拒绝。 */
	IRISCORE_API virtual void Filter(FNetObjectFilteringParams&) override;
};

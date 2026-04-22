// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Iris/ReplicationSystem/Filtering/NetObjectFilter.h"
#include "FilterOutNetObjectFilter.generated.h"

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
UCLASS()
class UFilterOutNetObjectFilter final : public UNetObjectFilter
{
	GENERATED_BODY()

protected:
	// UNetObjectFilter interface
	virtual void OnInit(const FNetObjectFilterInitParams&) override {}
	virtual void OnDeinit() override {}
	virtual void OnMaxInternalNetRefIndexIncreased(uint32 NewMaxInternalIndex) override {}
	virtual bool AddObject(uint32 ObjectIndex, FNetObjectFilterAddObjectParams&) override { return true; }
	virtual void RemoveObject(uint32 ObjectIndex, const FNetObjectFilteringInfo&) override {}

	IRISCORE_API virtual void Filter(FNetObjectFilteringParams&) override;
};

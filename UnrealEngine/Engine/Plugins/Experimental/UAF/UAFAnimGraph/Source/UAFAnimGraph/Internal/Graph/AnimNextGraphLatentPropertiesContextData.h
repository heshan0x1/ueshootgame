// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AnimNextGraphContextData.h"
#include "Module/AnimNextModuleContextData.h"
#include "Graph/AnimNextGraphInstance.h"
#include "AnimNextGraphLatentPropertiesContextData.generated.h"

namespace UE::UAF
{
	struct FLatentPropertyHandle;
}

USTRUCT()
struct FAnimNextGraphLatentPropertiesContextData : public FAnimNextGraphContextData
{
	GENERATED_BODY()

	FAnimNextGraphLatentPropertiesContextData() = default;

	FAnimNextGraphLatentPropertiesContextData(FAnimNextModuleInstance* InModuleInstance, const FAnimNextGraphInstance* InInstance, const TConstArrayView<UE::UAF::FLatentPropertyHandle>& InLatentHandles, void* InDestinationBasePtr, bool bInIsFrozen, bool bInJustBecameRelevant)
		: FAnimNextGraphContextData(InModuleInstance, InInstance)
		, LatentHandles(InLatentHandles)
		, DestinationBasePtr(InDestinationBasePtr)
		, bIsFrozen(bInIsFrozen)
		, bJustBecameRelevant(bInJustBecameRelevant)
	{
	}

	const TConstArrayView<UE::UAF::FLatentPropertyHandle>& GetLatentHandles() const { return LatentHandles; }
	void* GetDestinationBasePtr() const { return DestinationBasePtr; }
	bool IsFrozen() const { return bIsFrozen; }
	bool JustBecameRelevant() const { return bJustBecameRelevant; }

private:
	TConstArrayView<UE::UAF::FLatentPropertyHandle> LatentHandles;
	void* DestinationBasePtr = nullptr;
	bool bIsFrozen = false;
	bool bJustBecameRelevant = false;

	friend struct FAnimNextGraphInstance;
	friend struct FAnimNextExecuteContext;
};

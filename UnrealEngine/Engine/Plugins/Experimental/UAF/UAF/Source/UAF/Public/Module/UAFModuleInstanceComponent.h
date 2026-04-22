// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UAFAssetInstanceComponent.h"
#include "UAFModuleInstanceComponent.generated.h"

struct FAnimNextModuleInstance;
struct FAnimNextTraitEvent;

#define UE_API UAF_API

/** A module instance component is attached and owned by a module instance. */
USTRUCT()
struct FUAFModuleInstanceComponent : public FUAFAssetInstanceComponent
{
	GENERATED_BODY()

	using ContainerType = FAnimNextModuleInstance;

	FUAFModuleInstanceComponent() = default;

	virtual ~FUAFModuleInstanceComponent() override = default;

	// Returns the owning module instance this component lives on
	UE_API FAnimNextModuleInstance* GetModuleInstancePtr();

	// Returns the owning module instance this component lives on
	UE_API FAnimNextModuleInstance& GetModuleInstance();

	// Returns the owning module instance this component lives on
	UE_API const FAnimNextModuleInstance& GetModuleInstance() const;

	// Called during module execution for any events to be handled
	virtual void OnTraitEvent(FAnimNextTraitEvent& Event) {}

	// Called at end of module execution each frame
	virtual void OnEndExecution(float InDeltaTime) {}

private:
	friend struct FAnimNextModuleInstance;
};

#undef UE_API
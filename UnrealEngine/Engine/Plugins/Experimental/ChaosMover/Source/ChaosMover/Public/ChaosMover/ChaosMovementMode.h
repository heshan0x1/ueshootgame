// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ChaosMover/ChaosMoverSimulationTypes.h"
#include "MovementMode.h"

#include "ChaosMovementMode.generated.h"

#define UE_API CHAOSMOVER_API

class UChaosMoverSimulation;
namespace Chaos
{
	class FCollisionContactModifier;
}

UENUM()
enum class EChaosMoverIgnoredCollisionMode : uint8
{
	EnableCollisionsWithIgnored,
	DisableCollisionsWithIgnored,
};

/**
 * Base class for all Chaos movement modes
 */
UCLASS(MinimalAPI, Abstract, Blueprintable, BlueprintType, EditInlineNew, DefaultToInstanced)
class UChaosMovementMode : public UBaseMovementMode
{
	GENERATED_BODY()

public:
	UE_API UChaosMovementMode(const FObjectInitializer& ObjectInitializer);

	UFUNCTION(BlueprintPure, Category = ChaosMover)
	const UChaosMoverSimulation* GetSimulation() const
	{
		return Simulation;
	}

	UE_API void SetSimulation(UChaosMoverSimulation* InSimulation);

	// Whether this movement mode is relative to a basis transform
	virtual bool UsesMovementBasisTransform() const
	{
		return false;
	}

	// Whether the mode allows teleportation to the target transform
	// This is for mode specific teleportation tests, in addition to those already done prior to this check
	virtual bool CanTeleport(const FTransform& TargetTransform, const FMoverSyncState& CurrentSyncState) const
	{
		return true;
	}

	virtual void ModifyContacts(const FMoverTimeStep& TimeStep, const UE::ChaosMover::FSimulationInputData& InputData, const UE::ChaosMover::FSimulationOutputData& OutputData, Chaos::FCollisionContactModifier& Modifier) const
	{
	}

	UPROPERTY(EditAnywhere, Category = "Collision Settings")
	EChaosMoverIgnoredCollisionMode IgnoredCollisionMode = EChaosMoverIgnoredCollisionMode::DisableCollisionsWithIgnored;

protected:
	UChaosMoverSimulation* Simulation;
};

#undef UE_API

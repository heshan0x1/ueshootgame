// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ChaosMover/Character/Modes/ChaosCharacterMovementMode.h"

#include "ChaosFlyingMode.generated.h"

#define UE_API CHAOSMOVER_API

/**
 * Chaos character flying mode
 */
UCLASS(MinimalAPI, Blueprintable, BlueprintType, EditInlineNew, DefaultToInstanced)
class UChaosFlyingMode : public UChaosCharacterMovementMode
{
	GENERATED_BODY()

public:
	UE_API UChaosFlyingMode(const FObjectInitializer& ObjectInitializer);

	UE_API virtual void GenerateMove_Implementation(const FMoverTickStartData& StartState, const FMoverTimeStep& TimeStep, FProposedMove& OutProposedMove) const override;
	UE_API virtual void SimulationTick_Implementation(const FSimulationTickParams& Params, FMoverTickEndData& OutputState) override;
};

#undef UE_API

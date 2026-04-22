// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Chaos/ParticleHandleFwd.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "ChaosGroundMovementUtils.generated.h"

#define UE_API CHAOSMOVER_API

struct FFloorCheckResult;

UCLASS(MinimalAPI)
class UChaosGroundMovementUtils : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Computes the local velocity at the supplied position of the hit object in floor result */
	UFUNCTION(BlueprintCallable, Category = ChaosMover)
	static UE_API FVector ComputeLocalGroundVelocity_Internal(const FVector& Position, const FFloorCheckResult& FloorResult);

	static UE_API Chaos::FPBDRigidParticleHandle* GetRigidParticleHandleFromFloorResult_Internal(const FFloorCheckResult& FloorResult);
};

#undef UE_API

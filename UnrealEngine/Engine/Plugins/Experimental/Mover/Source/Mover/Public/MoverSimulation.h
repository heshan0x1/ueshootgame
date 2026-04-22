// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Object.h"

#include "MoverSimulation.generated.h"

#define UE_API MOVER_API

class UMoverBlackboard;
struct FMoverSyncState;
class URollbackBlackboard_InternalWrapper;

/**
* WIP Base class for a Mover simulation.
* The simulation is intended to be the thing that updates the Mover
* state and should be safe to run on an async thread
*/
UCLASS(MinimalAPI, BlueprintType)
class UMoverSimulation : public UObject
{
	GENERATED_BODY()

public:
	UE_API UMoverSimulation();

	// Warning: the regular blackboard will be fully replaced by the rollback blackboard in the future
	UFUNCTION(BlueprintPure, Category = Mover)
	UE_API const UMoverBlackboard* GetBlackboard() const;

	// Warning: the regular blackboard will be fully replaced by the rollback blackboard in the future
	UFUNCTION(BlueprintPure, Category = Mover)
	UE_API UMoverBlackboard* GetBlackboard_Mutable();

	UFUNCTION(BlueprintPure, Category = Mover)
	UE_API const URollbackBlackboard_InternalWrapper* GetRollbackBlackboard() const;

	UFUNCTION(BlueprintPure, Category = Mover)
	UE_API URollbackBlackboard_InternalWrapper* GetRollbackBlackboard_Mutable();


	/**
	* Attempt to teleport to TargetTransform. The teleport is not guaranteed to happen. This function is meant to be called by an instant movement effect as part of its effect application.
	* If it succeeds a FTeleportSucceededEventData will be emitted, if it fails a FTeleportFailedEventData will be sent.
	* @param TimeStep The time step of the current step or substep being simulated. This will come from the ApplyMovementEffect function.
	* @param TargetTransform The transform to teleport to. In the case bUseActorRotation is true, the rotation of this transform will be ignored.
	* @param bUseActorRotation If true, the rotation will not be modified upon teleportation. If false, the rotation in TargetTransform will be used to orient the teleported.
	* @param OutputState This is the sync state that me modified as a result of the application of this effect. Like TimeStep, this should come from the ApplyMovementEffect function.
	*/
	UFUNCTION(BlueprintCallable, Category = Mover)
	virtual void AttemptTeleport(const FMoverTimeStep& TimeStep, const FTransform& TargetTransform, bool bUseActorRotation, FMoverSyncState& OutputState) {}


	// Used during initialization only
	UE_API void SetRollbackBlackboard(URollbackBlackboard_InternalWrapper* RollbackSimBlackboard);

protected:
	UPROPERTY(Transient)
	TObjectPtr<UMoverBlackboard> Blackboard = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<URollbackBlackboard_InternalWrapper> RollbackBlackboard = nullptr;

};

#undef UE_API

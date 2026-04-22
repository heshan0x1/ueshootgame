// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MoverComponent.h"
#include "DefaultMovementSet/MovementModifiers/StanceModifier.h"
#include "Components/SceneComponent.h"
#include "PhysicsMover/PhysicsMoverSimulationTypes.h"

#include "PhysicsStanceModifier.generated.h"

#define UE_API MOVER_API

/**
 * Physics specialized version of FStanceModifier
 */
USTRUCT(BlueprintType)
struct FPhysicsStanceModifier : public FStanceModifier
{
	GENERATED_BODY()

	// @return newly allocated copy of this FMovementModifier. Must be overridden by child classes
	UE_API virtual FMovementModifierBase* Clone() const override;

	UE_API virtual void NetSerialize(FArchive& Ar) override;

	UE_API virtual UScriptStruct* GetScriptStruct() const override;

	UE_API virtual FString ToSimpleString() const override;

	UE_API virtual void AddReferencedObjects(class FReferenceCollector& Collector) override;

	UE_API virtual bool CanExpand_Internal(UMoverComponent* MoverComp, USceneComponent* UpdatedComponent, const FMoverSyncState& InSyncState) const;

	static UE_API void OnPostSimulationTick(const FStanceModifier* Modifier, UMoverComponent* MoverComp, UPrimitiveComponent* UpdatedPrimitive,  bool bIsCrouching, bool& bPostProcessed, OUT bool& bStanceChanged);

	UE_API virtual void OnStart(UMoverComponent* MoverComp, const FMoverTimeStep& TimeStep, const FMoverSyncState& SyncState, const FMoverAuxStateContext& AuxState) override;

	UE_API virtual void OnEnd(UMoverComponent* MoverComp, const FMoverTimeStep& TimeStep, const FMoverSyncState& SyncState, const FMoverAuxStateContext& AuxState) override;

protected:
	void ApplyModifierMove(UMoverComponent* MoverComp, const FMoverSyncState& InSyncState, float OriginalHalfHeight, float NewHalfHeight, float NewRadius);
	
	void UpdateCapsule(Chaos::FPBDRigidParticleHandle* ParticleHandle, float NewHalfHeight, float NewRadius, float TargetHeight, float GroundClearance);

private:
	static const Chaos::FCapsule GenerateNewCapsule(float NewHalfHeight, float NewRadius, float NewGroundClearance, float TargetHeight);
};

#undef UE_API

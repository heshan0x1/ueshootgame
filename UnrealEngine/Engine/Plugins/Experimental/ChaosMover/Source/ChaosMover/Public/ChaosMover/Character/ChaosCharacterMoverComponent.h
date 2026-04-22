// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ChaosMover/ChaosMoverSimulationTypes.h"
#include "DefaultMovementSet/CharacterMoverComponent.h"
#include "MoveLibrary/FloorQueryUtils.h"

#include "ChaosCharacterMoverComponent.generated.h"

// Fired after the actor lands on a valid surface. First param is the name of the mode this actor is in after landing. Second param is the hit result from hitting the floor.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FChaosMover_OnLanded, const FName&, NextMovementModeName, const FHitResult&, HitResult);

// Fired after the actor jumps. First param is the starting jump height.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FChaosMover_OnJumped, float, StartingJumpHeight);


UCLASS(MinimalAPI, BlueprintType, Blueprintable, meta = (BlueprintSpawnableComponent))
class UChaosCharacterMoverComponent : public UCharacterMoverComponent
{
	GENERATED_BODY()

public:
	CHAOSMOVER_API UChaosCharacterMoverComponent();

	CHAOSMOVER_API virtual bool TryGetFloorCheckHitResult(FHitResult& OutHitResult) const override;
	CHAOSMOVER_API virtual void ProduceInput(const int32 DeltaTimeMS, FMoverInputCmdContext* Cmd) override;
	CHAOSMOVER_API virtual void DoQueueNextMode(FName DesiredModeName, bool bShouldReenter=false) override;

	CHAOSMOVER_API virtual void Crouch() override;
	CHAOSMOVER_API virtual void UnCrouch() override;

	// Broadcast when this actor lands on a valid surface.
	UPROPERTY(BlueprintAssignable, Category = "Chaos Mover")
	FChaosMover_OnLanded OnLanded;

	// Broadcast when this actor jumps.
	UPROPERTY(BlueprintAssignable, Category = "Chaos Mover")
	FChaosMover_OnJumped OnJumped;

	// Launch the character using either impulse or velocity
	// Note: This will only trigger a launch if a launch transition is implemented on the current movement mode
	UFUNCTION(BlueprintCallable, Category = "Chaos Mover")
	void Launch(const FVector& VelocityOrImpulse, EChaosMoverVelocityEffectMode Mode = EChaosMoverVelocityEffectMode::AdditiveVelocity);

	// Override the movement mode settings on a mode
	// If the name is not set it will apply to the current mode
	UFUNCTION(BlueprintCallable, Category = "Chaos Mover")
	CHAOSMOVER_API void OverrideMovementSettings(const FChaosMovementSettingsOverrides Overrides);

	// Cancel overrides of movement mode settings
	// If the name is not set it will apply to the current mode
	UFUNCTION(BlueprintCallable, Category = "Chaos Mover")
	void CancelMovementSettingsOverrides(FName ModeName = NAME_None);

protected:
	CHAOSMOVER_API virtual void OnMoverPreSimulationTick(const FMoverTimeStep& TimeStep, const FMoverInputCmdContext& InputCmd) override;
	CHAOSMOVER_API virtual void ProcessSimulationEvent(const FMoverSimulationEventData& EventData) override;
	CHAOSMOVER_API virtual void SetAdditionalSimulationOutput(const FMoverDataCollection& Data) override;

	CHAOSMOVER_API void ClearQueuedMode();
	CHAOSMOVER_API virtual void OnStanceModified(const FStanceModifiedEventData& EventData);

	FName ModeToOverrideSettings = NAME_None;
	float MaxSpeedOverride = 0.0f;
	float AccelerationOverride = 0.0f;
	bool bOverrideMovementSettings = false;
	bool bCancelMovementOverrides = false;

	bool bFloorResultSet = false;
	FFloorCheckResult LatestFloorResult;

	FVector LaunchVelocityOrImpulse = FVector::ZeroVector;
	EChaosMoverVelocityEffectMode LaunchMode = EChaosMoverVelocityEffectMode::AdditiveVelocity;

	// Queued immediate mode transition, it will be transmitted to the PT next time ProduceInput is called
	FName QueuedModeTransitionName = NAME_None;

	bool bCrouchInputPending = false;
};

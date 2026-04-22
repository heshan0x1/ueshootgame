// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Backends/MoverBackendLiaison.h"
#include "Chaos/Character/CharacterGroundConstraint.h"
#include "Chaos/Declares.h"
#include "ChaosMover/ChaosMoverSimulation.h"
#include "ChaosMover/ChaosMoverSimulationTypes.h"
#include "ChaosMover/PathedMovement/ChaosPathedMovementTypes.h"
#include "Components/ActorComponent.h"

#include "ChaosMoverBackend.generated.h"

class UMoverComponent;
class UNetworkPhysicsComponent;

namespace Chaos
{
	class FCharacterGroundConstraint;
	class FCharacterGroundConstraintProxy;

	class FJointConstraint;
	class FJointConstraintPhysicsProxy;
}

UCLASS(MinimalAPI, Within = MoverComponent)
class UChaosMoverBackendComponent : public UActorComponent, public IMoverBackendLiaisonInterface
{
	GENERATED_BODY()

public:
	CHAOSMOVER_API UChaosMoverBackendComponent();

	CHAOSMOVER_API virtual void InitializeComponent() override;
	CHAOSMOVER_API virtual void UninitializeComponent() override;
	CHAOSMOVER_API virtual void BeginPlay() override;
	CHAOSMOVER_API virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// IMoverBackendLiaisonInterface
	CHAOSMOVER_API virtual bool IsAsync() const override;
	CHAOSMOVER_API virtual float GetEventSchedulingMinDelaySeconds() const override;
	CHAOSMOVER_API virtual double GetCurrentSimTimeMs();
	CHAOSMOVER_API virtual int32 GetCurrentSimFrame();

	CHAOSMOVER_API virtual void ProduceInputData(int32 PhysicsStep, int32 NumSteps, const FMoverTimeStep& TimeStep, UE::ChaosMover::FSimulationInputData& InputData);
	CHAOSMOVER_API virtual void ConsumeOutputData(const FMoverTimeStep& TimeStep, const UE::ChaosMover::FSimulationOutputData& OutputData);
	CHAOSMOVER_API virtual void FinalizeFrame(double ResultsTimeInMs);

	CHAOSMOVER_API UChaosMoverSimulation* GetSimulation();
	CHAOSMOVER_API const UChaosMoverSimulation* GetSimulation() const;

protected:
	CHAOSMOVER_API virtual void InitSimulation();
	CHAOSMOVER_API virtual void DeinitSimulation();
	CHAOSMOVER_API virtual void CreatePhysics();
	CHAOSMOVER_API virtual void DestroyPhysics();

	CHAOSMOVER_API void CreateCharacterGroundConstraint();
	CHAOSMOVER_API void DestroyCharacterGroundConstraint();

	CHAOSMOVER_API void CreateActuationConstraint();
	CHAOSMOVER_API void DestroyActuationConstraint();

	CHAOSMOVER_API virtual void GenerateInput(const FMoverTimeStep& TimeStep, UE::ChaosMover::FSimulationInputData& InputData);

	CHAOSMOVER_API UMoverComponent& GetMoverComponent() const;
	CHAOSMOVER_API Chaos::FPhysicsSolver* GetPhysicsSolver() const;
	CHAOSMOVER_API Chaos::FPhysicsObject* GetPhysicsObject() const;
	CHAOSMOVER_API Chaos::FPBDRigidParticle* GetControlledParticle() const;

	UFUNCTION()
	CHAOSMOVER_API virtual void HandleUpdatedComponentPhysicsStateChanged(UPrimitiveComponent* ChangedComponent, EComponentPhysicsStateChange StateChange);

	UFUNCTION()
	CHAOSMOVER_API void HandleOwningPawnControllerChanged_Server(APawn* OwnerPawn, AController* OldController, AController* NewController);

	TSubclassOf<UChaosMoverSimulation> SimulationClass;

	UPROPERTY(Transient)
	TObjectPtr<UNetworkPhysicsComponent> NetworkPhysicsComponent;

	UPROPERTY(Transient)
	TObjectPtr<UNullMovementMode> NullMovementMode;

	UPROPERTY(Transient)
	TObjectPtr<UImmediateMovementModeTransition> ImmediateModeTransition;

	UPROPERTY(Transient)
	TObjectPtr<UChaosMoverSimulation> Simulation;

	UE::Mover::FSimulationOutputRecord SimOutputRecord;

	// Character ground constraint, for moving on ground like characters
	TUniquePtr<Chaos::FCharacterGroundConstraint> CharacterGroundConstraint;

	// General purpose joint constraint, for moving the controlled component physically
	FPhysicsConstraintHandle ActuationConstraintHandle;
	FConstraintInstance ActuationConstraintInstance;
	FPhysicsUserData ActuationConstraintPhysicsUserData;

	FTransform TransformOnInit;

	bool bIsUsingAsyncPhysics = false;
	bool bWantsDestroySim = false;
	bool bWantsCreateSim = true;

	// Transfers queued instant movement effects from the mover component to the simulation input.
	// This is used in the input producing case.
	void InjectInstantMovementEffectsIntoInput(const FMoverTimeStep& TimeStep, UE::ChaosMover::FSimulationInputData& InputData);
	
	// Transfers queued instant movement effects from the mover component to the simulation directly, bypassing the input.
	// This is used in the non input producing case.
	void InjectInstantMovementEffectsIntoSim(const FMoverTimeStep& TimeStep);

private:
	// Unique ID counter for instant movement effects.
	// Unique IDs need to be generated because input can be repeated and we need to prevent multiple application of a given effect only queued once.
	uint8 NextInstantMovementEffectUniqueID = 0xFF;
};
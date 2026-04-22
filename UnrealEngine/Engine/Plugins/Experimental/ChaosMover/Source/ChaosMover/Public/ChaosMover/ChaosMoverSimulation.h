// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Chaos/Declares.h"
#include "ChaosMover/ChaosMoverSimulationTypes.h"
#include "ChaosMover/PathedMovement/ChaosPathedMovementTypes.h"
#include "ChaosMover/ChaosMoverStateMachine.h"
#include "MoveLibrary/MovementMixer.h"
#include "MoveLibrary/MoverBlackboard.h"
#include "MoverTypes.h"
#include "MoverSimulation.h"
#include "PhysicsEngine/ConstraintInstance.h"
#include "UObject/WeakObjectPtrTemplates.h"

#include "ChaosMoverSimulation.generated.h"

namespace Chaos
{
	class FCharacterGroundConstraintHandle;
	class FPBDJointConstraintHandle;
	class FCollisionContactModifier;
}

UCLASS(MinimalAPI, BlueprintType)
class UChaosMoverSimulation : public UMoverSimulation
{
	GENERATED_BODY()

public:
	CHAOSMOVER_API UChaosMoverSimulation();

	// Returns the local simulation input MoverDataCollection, to read local non networked data passed to the simulation by the gameplay thread
	UFUNCTION(BlueprintPure, Category = Mover)
	CHAOSMOVER_API const FMoverDataCollection& GetLocalSimInput() const;

	// Returns the local simulation input MoverDataCollection, to pass local non networked data to the simulation
	// Only available from the gameplay thread
	UFUNCTION(BlueprintPure, Category = Mover)
	CHAOSMOVER_API FMoverDataCollection& GetLocalSimInput_Mutable();

	UFUNCTION(BlueprintPure, Category = Mover)
	CHAOSMOVER_API const UBaseMovementMode* GetCurrentMovementMode() const;

	UFUNCTION(BlueprintPure, Category = Mover)
	CHAOSMOVER_API const UBaseMovementMode* FindMovementModeByName(const FName& Name) const;

	UFUNCTION(BlueprintPure, Category = Mover)
	CHAOSMOVER_API UBaseMovementMode* FindMovementModeByName_Mutable(const FName& Name);

	UFUNCTION(BlueprintCallable, CustomThunk, Category = Mover, meta = (CustomStructureParam = "EffectAsRawData", AllowAbstract = "false", DisplayName = "Queue Instant Movement Effect"))
	void K2_QueueInstantMovementEffect(UPARAM(DisplayName = "Instant Movement Effect") const int32& EffectAsRawData);
	DECLARE_FUNCTION(execK2_QueueInstantMovementEffect);

	// Queue an Instant Movement Effect to take place at the end of this frame or start of the next subtick - whichever happens first
	CHAOSMOVER_API void QueueInstantMovementEffect(TSharedPtr<FInstantMovementEffect> InstantMovementEffect, bool bShouldRollBack = true);
	// Queue a scheduled instant movement effect
	CHAOSMOVER_API void QueueInstantMovementEffect(const FScheduledInstantMovementEffect& ScheduledInstantMovementEffect, bool bShouldRollBack = true);

public:
	UFUNCTION(BlueprintCallable, CustomThunk, Category = Mover, meta = (CustomStructureParam = "MoveAsRawData", AllowAbstract = "false", DisplayName = "Queue Movement Modifier"))
	CHAOSMOVER_API FMovementModifierHandle K2_QueueMovementModifier(UPARAM(DisplayName = "Movement Modifier") const int32& MoveAsRawData);
	DECLARE_FUNCTION(execK2_QueueMovementModifier);

	// Queue a Movement Modifier to start during the next simulation frame.
	CHAOSMOVER_API FMovementModifierHandle QueueMovementModifier(TSharedPtr<FMovementModifierBase> Modifier);

	// Find movement modifier by type (returns the first modifier it finds). Returns nullptr if the modifier couldn't be found
	CHAOSMOVER_API const FMovementModifierBase* FindMovementModifierByType(const UScriptStruct* DataStructType) const;

	/** Find a movement modifier of a specific type in this components movement modifiers. If not found, null will be returned. */
	template <typename ModifierT = FMovementModifierBase UE_REQUIRES(std::is_base_of_v<FMovementModifierBase, ModifierT>)>
	const ModifierT* FindMovementModifierByType() const { return static_cast<const ModifierT*>(FindMovementModifierByType(ModifierT::StaticStruct())); }

	UFUNCTION(BlueprintCallable, Category = Mover)
	CHAOSMOVER_API void CancelModifierFromHandle(FMovementModifierHandle ModifierHandle);

	UFUNCTION(BlueprintPure, Category = Mover, meta = (Keywords = "HasTag"))
	CHAOSMOVER_API bool HasGameplayTag(FGameplayTag TagToFind, bool bExactMatch) const;

	struct FInitParams
	{
		TMap<FName, TWeakObjectPtr<UBaseMovementMode>> ModesToRegister;
		TArray<TWeakObjectPtr<UBaseMovementModeTransition>> TransitionsToRegister;
		FMoverSyncState InitialSyncState;
		FName StartingMovementMode = NAME_None;
		TWeakObjectPtr<UNullMovementMode> NullMovementMode = nullptr;
		TWeakObjectPtr<UImmediateMovementModeTransition> ImmediateModeTransition = nullptr;
		TWeakObjectPtr<UMovementMixer> MovementMixer = nullptr;
		FTransform TransformOnInit = FTransform::Identity;
		Chaos::FCharacterGroundConstraintHandle* CharacterConstraintHandle = nullptr;
		Chaos::FPBDJointConstraintHandle* ActuationConstraintHandle = nullptr;
		Chaos::FKinematicGeometryParticleHandle* ActuationConstraintEndPointParticleHandle = nullptr;
		Chaos::FConstPhysicsObjectHandle PhysicsObject = nullptr;
		Chaos::FPhysicsSolver* Solver = nullptr;
		UWorld* World = nullptr;
	};

	CHAOSMOVER_API void Init(const FInitParams& InitParams);
	CHAOSMOVER_API void Deinit();

	CHAOSMOVER_API void ProcessInputs(int32 PhysicsStep, const FMoverTimeStep& TimeStep, const UE::ChaosMover::FSimulationInputData& InputData);
	CHAOSMOVER_API void SimulationTick(const FMoverTimeStep& TimeStep, const UE::ChaosMover::FSimulationInputData& InputData, UE::ChaosMover::FSimulationOutputData& OutputData);
	CHAOSMOVER_API void ModifyContacts(const FMoverTimeStep& TimeStep, const UE::ChaosMover::FSimulationInputData& InputData, const UE::ChaosMover::FSimulationOutputData& OutputData, Chaos::FCollisionContactModifier& Modifier);

	CHAOSMOVER_API void AddEvent(TSharedPtr<FMoverSimulationEventData> Event);

	CHAOSMOVER_API void ApplyNetInputData(const FMoverInputCmdContext& InNetInputCmd);
	CHAOSMOVER_API void BuildNetInputData(FMoverInputCmdContext& OutNetInputCmd) const;
	CHAOSMOVER_API void ApplyNetStateData(const FMoverSyncState& InNetSyncState);
	CHAOSMOVER_API void BuildNetStateData(FMoverSyncState& OutNetSyncState) const;

	// Movement modes can be relative to a basis transform, which can change at runtime
	// Returns the movement basis transform, for movement relative to it
	CHAOSMOVER_API virtual const FTransform& GetMovementBasisTransform() const;
	// Sets the movement basis transform, for movement relative to it
	CHAOSMOVER_API virtual void SetMovementBasisTransform(const FTransform& InMovementBasisTransform);

	//~ Debugging Util functions
	// Collection for holding extra debug data, that will be sent to the Chaos Visual Debugger for debugging
	UFUNCTION(BlueprintPure, Category = Mover)
	CHAOSMOVER_API FMoverDataCollection& GetDebugSimData();
	//~ End of Debugging Util functions

protected:
	CHAOSMOVER_API virtual void OnInit();
	CHAOSMOVER_API virtual void OnDeinit();
	CHAOSMOVER_API virtual void OnPreSimulationTick(const FMoverTimeStep& TimeStep, const UE::ChaosMover::FSimulationInputData& InputData);
	CHAOSMOVER_API virtual void OnSimulationTick(const FMoverTimeStep& TimeStep, const UE::ChaosMover::FSimulationInputData& InputData, UE::ChaosMover::FSimulationOutputData& OutputData);
	CHAOSMOVER_API virtual void OnPostSimulationTick(const FMoverTimeStep& TimeStep, UE::ChaosMover::FSimulationOutputData& OutputData);
	CHAOSMOVER_API virtual void OnModifyContacts(const FMoverTimeStep& TimeStep, const UE::ChaosMover::FSimulationInputData& InputData, const UE::ChaosMover::FSimulationOutputData& OutputData, Chaos::FCollisionContactModifier& Modifier);
	CHAOSMOVER_API virtual void OnSimulationRollback(const FMoverTimeStep& NewTimeStep, const FMoverSyncState& PrevSyncState);

	// Character-like movement sim steps
	CHAOSMOVER_API virtual void PreSimulationTickCharacter(const IChaosCharacterMovementModeInterface& CharacterMode, const FMoverTimeStep& TimeStep, const UE::ChaosMover::FSimulationInputData& InputData);
	CHAOSMOVER_API virtual void PostSimulationTickCharacter(const IChaosCharacterMovementModeInterface& CharacterMode, const FMoverTimeStep& TimeStep, UE::ChaosMover::FSimulationOutputData& OutputData);
	CHAOSMOVER_API virtual void PostSimulationTickCharacterConstraint(const IChaosCharacterConstraintMovementModeInterface& CharacterConstraintMode, const FMoverTimeStep& TimeStep, UE::ChaosMover::FSimulationOutputData& OutputData);

	// Pathed movement sim steps (kinematic or constrained)
	CHAOSMOVER_API virtual void PostSimulationTickMovementActuation(const IChaosMovementActuationInterface& ConstraintMode, const FMoverTimeStep& TimeStep, UE::ChaosMover::FSimulationOutputData& OutputData);

	CHAOSMOVER_API Chaos::FPBDRigidParticleHandle* GetControlledParticle() const;

	CHAOSMOVER_API virtual void ProcessSimulationEvent(const FMoverSimulationEventData& ModeChangedData);
	CHAOSMOVER_API virtual void OnMovementModeChanged(const FMovementModeChangedEventData& ModeChangedData);

	// Teleportation (internal functions)
public:
	// Attempt to teleport. This will first check CanTeleport and broadcast simulation events to indicate success or failure.
	CHAOSMOVER_API virtual void AttemptTeleport(const FMoverTimeStep& TimeStep, const FTransform& TargetTransform, bool bUseActorRotation, FMoverSyncState& OutputState) override;
protected:
	// Whether the teleport can happen (does the movement mode allow it? Do we fit at the target transform?)
	CHAOSMOVER_API virtual bool CanTeleport(const FTransform& TargetTransform, bool bUseActorRotation, const FMoverSyncState& SyncState);
	// Actually perform the teleport
	CHAOSMOVER_API virtual void Teleport(const FTransform& TargetTransform, const FMoverTimeStep& TimeStep, FMoverSyncState& OutputState);

	// Object State.
	// In kinematic mode, the updated primitive component needs both its SimulatePhysics and UpdateKinematicFromSimulation options checked
	CHAOSMOVER_API void SetControlledParticleDynamic();
	CHAOSMOVER_API void SetControlledParticleKinematic();
	CHAOSMOVER_API bool IsControlledParticleDynamic() const;
	CHAOSMOVER_API bool IsControlledParticleKinematic() const;

	// Character ground constraint
	CHAOSMOVER_API void EnableCharacterConstraint();
	CHAOSMOVER_API void DisableCharacterConstraint();
	CHAOSMOVER_API bool IsCharacterConstraintEnabled() const;

	// General purpose actuation joint constraint
	CHAOSMOVER_API void EnableActuationConstraint();
	CHAOSMOVER_API void DisableActuationConstraint();
	CHAOSMOVER_API bool IsActuationConstraintEnabled() const;
	CHAOSMOVER_API void SetActuationTargetTransform(const FTransform& TargetTransform);
	CHAOSMOVER_API void TeleportActuationTarget(const FTransform& TargetTransform, bool AlsoTeleportControlledParticle = false);
	
	CHAOSMOVER_API void TraceMoverData(const FMoverTimeStep& TimeStep, const UE::ChaosMover::FSimulationOutputData& OutputData);

	// State structs
	FMoverSyncState CurrentSyncState;
	// Data internal to the simulation
	FMoverDataCollection InternalSimData;
	// Local input data, usually sent by the gameplay side locally, that is not expected to differ from that on the server so doesn't warrant networking
	FMoverDataCollection LocalSimInput;
	// Debug Data collection, sent to Chaos Visual Debugger when Trace Extra Sim Debug Data is selected
	FMoverDataCollection DebugSimData;

	// Movement mode state machine
	UE::ChaosMover::FMoverStateMachine StateMachine;

	// Optional movement mixer
	TWeakObjectPtr<UMovementMixer> MovementMixerWeakPtr = nullptr;

	// Controlled physics object
	Chaos::FConstPhysicsObjectHandle PhysicsObject = nullptr;

	// Character ground constraint, specifically for moving on ground like characters
	Chaos::FCharacterGroundConstraintHandle* CharacterConstraintHandle = nullptr;

	// General purpose actuation joint constraint
	Chaos::FPBDJointConstraintHandle* ActuationConstraintHandle = nullptr;
	Chaos::FKinematicGeometryParticleHandle* ActuationConstraintEndPointParticleHandle= nullptr;
	
	// Some movement modes are relative to a movement basis transform, stored here
	FTransform MovementBasisTransform = FTransform::Identity;

	Chaos::FPhysicsSolver* Solver = nullptr;
	UWorld* World = nullptr;

private:
	FMoverInputCmdContext NetInputCmd;
	FMoverSyncState NetSyncState;
	TArray<TSharedPtr<FMoverSimulationEventData>> Events;

	bool bInputCmdOverridden = false;
	bool bSyncStateOverridden = false;

	int32 InternalServerFrame = 0;

	bool bInitialized = false;
};

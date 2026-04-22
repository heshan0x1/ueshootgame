// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosMover/ChaosMoverSimulation.h"

#include "Chaos/ChaosEngineInterface.h"
#include "Chaos/Character/CharacterGroundConstraint.h"
#include "Chaos/Character/CharacterGroundConstraintContainer.h"
#include "Chaos/ContactModification.h"
#include "Chaos/KinematicTargets.h"
#include "Chaos/PBDJointConstraintData.h"
#include "Chaos/PhysicsObjectInternalInterface.h"
#include "ChaosMover/ChaosMovementMode.h"
#include "ChaosMover/ChaosMovementModeTransition.h"
#include "ChaosMover/ChaosMoverConsoleVariables.h"
#include "ChaosMover/ChaosMoverLog.h"
#include "ChaosMover/ChaosMoverSimulationTypes.h"
#include "ChaosMover/Character/ChaosCharacterInputs.h"
#include "ChaosMover/PathedMovement/ChaosPathedMovementTypes.h"
#include "ChaosMover/Utilities/ChaosGroundMovementUtils.h"
#include "ChaosMover/Utilities/ChaosMoverQueryUtils.h"
#include "DefaultMovementSet/CharacterMoverSimulationTypes.h"
#include "Engine/World.h"
#include "Framework/Threading.h"
#include "MoveLibrary/FloorQueryUtils.h"
#include "MoveLibrary/MovementMixer.h"
#include "MoveLibrary/MoverBlackboard.h"
#include "MoveLibrary/RollbackBlackboard.h"
#include "MoveLibrary/WaterMovementUtils.h"
#include "MovementModeStateMachine.h"
#include "MoverSimulationTypes.h"
#include "PBDRigidsSolver.h"
#include "Physics/Experimental/PhysScene_Chaos.h"
#include "PhysicsEngine/PhysicsObjectExternalInterface.h"
#include "PhysicsProxy/CharacterGroundConstraintProxy.h"
#include "UObject/UObjectGlobals.h"

#if WITH_CHAOS_VISUAL_DEBUGGER
#include "ChaosVisualDebugger/ChaosVisualDebuggerTrace.h"
#include "ChaosVisualDebugger/MoverCVDRuntimeTrace.h"
#endif

#include UE_INLINE_GENERATED_CPP_BY_NAME(ChaosMoverSimulation)

UChaosMoverSimulation::UChaosMoverSimulation()
{
}

const FMoverDataCollection& UChaosMoverSimulation::GetLocalSimInput() const
{
	return LocalSimInput;
}

FMoverDataCollection& UChaosMoverSimulation::GetLocalSimInput_Mutable()
{
	// Only the Gameplay Thread is allowed to write to the local simulation input data collection
	Chaos::EnsureIsInGameThreadContext();

	return LocalSimInput;
}

FMoverDataCollection& UChaosMoverSimulation::GetDebugSimData()
{
	return DebugSimData;
}

const UBaseMovementMode* UChaosMoverSimulation::GetCurrentMovementMode() const
{
	return StateMachine.GetCurrentMode().Get();
}

const UBaseMovementMode* UChaosMoverSimulation::FindMovementModeByName(const FName& Name) const
{
	return StateMachine.FindMovementMode(Name).Get();
}

UBaseMovementMode* UChaosMoverSimulation::FindMovementModeByName_Mutable(const FName& Name)
{
	return StateMachine.FindMovementMode_Mutable(Name).Get();
}

void UChaosMoverSimulation::ApplyNetInputData(const FMoverInputCmdContext& InNetInputCmd)
{
	NetInputCmd = InNetInputCmd;
	bInputCmdOverridden = true;
}

void UChaosMoverSimulation::BuildNetInputData(FMoverInputCmdContext& OutNetInputCmd) const
{
	OutNetInputCmd = NetInputCmd;
}

void UChaosMoverSimulation::ApplyNetStateData(const FMoverSyncState& InNetSyncState)
{
	NetSyncState = InNetSyncState;
	bSyncStateOverridden = true;
}

void UChaosMoverSimulation::BuildNetStateData(FMoverSyncState& OutNetSyncState) const
{
	OutNetSyncState = NetSyncState;
}

void UChaosMoverSimulation::Init(const FInitParams& InitParams)
{
	// Only the Gameplay Thread is allowed to Init the chaos mover simulation
	Chaos::EnsureIsInGameThreadContext();

	MovementMixerWeakPtr = InitParams.MovementMixer.Get();
	CharacterConstraintHandle = InitParams.CharacterConstraintHandle;
	ActuationConstraintHandle = InitParams.ActuationConstraintHandle;
	ActuationConstraintEndPointParticleHandle = InitParams.ActuationConstraintEndPointParticleHandle;
	PhysicsObject = InitParams.PhysicsObject;
	Solver = InitParams.Solver;
	World = InitParams.World;

	CurrentSyncState = InitParams.InitialSyncState;

	// Set the pathed movement basis to the initial location of the controlled particle
	// We set this up before state machine init so the pathed movement modes have a pathed movement basis to work with on registration
	FTransform NewMovementBasisTransform(InitParams.TransformOnInit.GetRotation(), InitParams.TransformOnInit.GetLocation());
	SetMovementBasisTransform(NewMovementBasisTransform);

	UE::ChaosMover::FMoverStateMachine::FInitParams StateMachineInitParams;
	StateMachineInitParams.ImmediateMovementModeTransition = InitParams.ImmediateModeTransition;
	StateMachineInitParams.NullMovementMode = InitParams.NullMovementMode;
	StateMachineInitParams.Simulation = this;
	StateMachine.Init(StateMachineInitParams);

	for (const TPair<FName, TWeakObjectPtr<UBaseMovementMode>>& Element : InitParams.ModesToRegister)
	{
		const FName& ModeName = Element.Key;
		TWeakObjectPtr<UBaseMovementMode> Mode = Element.Value;

		if (Mode.Get() == nullptr)
		{
			UE_LOG(LogChaosMover, Warning, TEXT("Invalid Movement Mode type '%s' detected. Mover actor will not function correctly."), *ModeName.ToString());
			continue;
		}

		if (UChaosMovementMode* ChaosMode = Cast<UChaosMovementMode>(Mode.Get()))
		{
			ChaosMode->SetSimulation(this);
		}

		bool bIsDefaultMode = (InitParams.StartingMovementMode == ModeName);
		StateMachine.RegisterMovementMode(ModeName, Mode, bIsDefaultMode);
	}

	for (const TWeakObjectPtr<UBaseMovementModeTransition>& Transition : InitParams.TransitionsToRegister)
	{
		if (UChaosMovementModeTransition* ChaosTransition = Cast<UChaosMovementModeTransition>(Transition.Get()))
		{
			ChaosTransition->SetSimulation(this);
		}

		StateMachine.RegisterGlobalTransition(Transition);
	}

	StateMachine.QueueNextMode(StateMachine.GetDefaultModeName());

	OnInit();

	bInitialized = true;
}

void UChaosMoverSimulation::Deinit()
{
	bInitialized = false;

	OnDeinit();
}

void UChaosMoverSimulation::OnInit()
{
	
}

void UChaosMoverSimulation::OnDeinit()
{

}

void UChaosMoverSimulation::ProcessInputs(int32 PhysicsStep, const FMoverTimeStep& TimeStep, const UE::ChaosMover::FSimulationInputData& InputData)
{
	Chaos::EnsureIsInPhysicsThreadContext();

	// Ensure that we're using the correct inputs. On the server or during resim the inputs will
	// be overridden by the network physics component, so use them instead of the generated inputs.
	// On the client the input data is read out to be sent to the server so update the data that is read.
	if (bInputCmdOverridden)
	{
		InputData.InputCmd = NetInputCmd;
		bInputCmdOverridden = false;
	}
	else
	{
		NetInputCmd = InputData.InputCmd;
	}

	// Sync state is overwritten by network physics for sim proxies and for resimulation
	// In either case we rollback the simulation to invalidate the blackboard and reset
	// the state machine based on the new state
	if (bSyncStateOverridden)
	{
		const FMoverSyncState PrevSyncState = CurrentSyncState;
		CurrentSyncState = NetSyncState;
		OnSimulationRollback(TimeStep, PrevSyncState);
		bSyncStateOverridden = false;
	}
	else
	{
		NetSyncState = CurrentSyncState;
	}
}

void UChaosMoverSimulation::SimulationTick(const FMoverTimeStep& TimeStep, const UE::ChaosMover::FSimulationInputData& InputData, UE::ChaosMover::FSimulationOutputData& OutputData)
{
	Chaos::EnsureIsInPhysicsThreadContext();

	if (bInitialized)
	{
		RollbackBlackboard->BeginSimulationFrame(TimeStep);

		OnPreSimulationTick(TimeStep, InputData);
		OnSimulationTick(TimeStep, InputData, OutputData);
		OnPostSimulationTick(TimeStep, OutputData);

		RollbackBlackboard->EndSimulationFrame();
	}
}

void UChaosMoverSimulation::ModifyContacts(const FMoverTimeStep& TimeStep, const UE::ChaosMover::FSimulationInputData& InputData, const UE::ChaosMover::FSimulationOutputData& OutputData, Chaos::FCollisionContactModifier& Modifier)
{
	Chaos::EnsureIsInPhysicsThreadContext();

	if (!bInitialized)
	{
		return;
	}

	if (TStrongObjectPtr<const UBaseMovementMode> CurrentModePtr = StateMachine.GetCurrentMode().Pin())
	{
		if (const UChaosMovementMode* ChaosMode = Cast<UChaosMovementMode>(CurrentModePtr.Get()))
		{
			// Base contact modification
			// Disable collisions for actors and components on the ignore list in the query params
			if (ChaosMode->IgnoredCollisionMode == EChaosMoverIgnoredCollisionMode::DisableCollisionsWithIgnored)
			{
				Chaos::FGeometryParticleHandle* UpdatedParticle = nullptr;
				const FChaosMoverSimulationDefaultInputs* SimInputs = LocalSimInput.FindDataByType<FChaosMoverSimulationDefaultInputs>();
				if (SimInputs)
				{
					Chaos::FReadPhysicsObjectInterface_Internal ReadInterface = Chaos::FPhysicsObjectInternalInterface::GetRead();
					UpdatedParticle = ReadInterface.GetParticle(SimInputs->PhysicsObject);
				}

				if (!UpdatedParticle)
				{
					return;
				}

				for (Chaos::FContactPairModifier& PairModifier : Modifier.GetContacts(UpdatedParticle))
				{
					const int32 OtherIdx = UpdatedParticle == PairModifier.GetParticlePair()[0] ? 1 : 0;

					if (const Chaos::FShapeInstance* Shape = PairModifier.GetShape(OtherIdx))
					{
						const Chaos::Filter::FCombinedShapeFilterData& CombinedShapeFilter = ChaosInterface::GetCombinedShapeFilterData(*Shape);
						const Chaos::Filter::FInstanceData& InstanceData = CombinedShapeFilter.GetInstanceData();
						const Chaos::Filter::FShapeFilterData& FilterData = CombinedShapeFilter.GetShapeFilterData();
						uint32 ComponentID = InstanceData.GetComponentId();
						if (SimInputs->CollisionQueryParams.GetIgnoredComponents().Contains(ComponentID))
						{
							PairModifier.Disable();
							continue;
						}

						uint32 ActorID = InstanceData.GetActorId();
						if (SimInputs->CollisionQueryParams.GetIgnoredSourceObjects().Contains(ActorID))
						{
							PairModifier.Disable();
							continue;
						}

						FMaskFilter ShapeMaskFilter = FilterData.GetMaskFilter();
						if (SimInputs->CollisionQueryParams.IgnoreMask & ShapeMaskFilter)
						{
							PairModifier.Disable();
							continue;
						}
					}
				}
			}

			// Mode specific contact modification
			ChaosMode->ModifyContacts(TimeStep, InputData, OutputData, Modifier);
		}
	}

	OnModifyContacts(TimeStep, InputData, OutputData, Modifier);
}

void UChaosMoverSimulation::PreSimulationTickCharacter(const IChaosCharacterMovementModeInterface& CharacterMode, const FMoverTimeStep& TimeStep, const UE::ChaosMover::FSimulationInputData& InputData)
{
	// Add inputs if we don't have them and make sure we have a valid input
	FCharacterDefaultInputs& CharacterDefaultInputs = InputData.InputCmd.InputCollection.FindOrAddMutableDataByType<FCharacterDefaultInputs>();
	if (CharacterDefaultInputs.GetMoveInputType() == EMoveInputType::Invalid)
	{
		CharacterDefaultInputs.SetMoveInput(EMoveInputType::DirectionalIntent, FVector::ZeroVector);
	}

	if (!CharacterDefaultInputs.SuggestedMovementMode.IsNone())
	{
		StateMachine.QueueNextMode(CharacterDefaultInputs.SuggestedMovementMode);
		CharacterDefaultInputs.SuggestedMovementMode = NAME_None;
	}

	if (const FChaosMovementSettingsOverrides* MovementSettingsOverride = InputData.InputCmd.InputCollection.FindDataByType<FChaosMovementSettingsOverrides>())
	{
		// If the override contains a mode name find that mode to override. Otherwise take the latest mode,
		// or the pending mode if a new one has been set
		TWeakObjectPtr<UBaseMovementMode> ModePtr = !MovementSettingsOverride->ModeName.IsNone() ? StateMachine.FindMovementMode_Mutable(MovementSettingsOverride->ModeName) :
			(CharacterDefaultInputs.SuggestedMovementMode.IsNone() ? StateMachine.GetCurrentMode() : StateMachine.FindMovementMode_Mutable(CharacterDefaultInputs.SuggestedMovementMode));

		if (IChaosCharacterMovementModeInterface* ModeInterface = Cast<IChaosCharacterMovementModeInterface>(ModePtr.Get()))
		{
			ModeInterface->OverrideMaxSpeed(MovementSettingsOverride->MaxSpeedOverride);
			ModeInterface->OverrideAcceleration(MovementSettingsOverride->AccelerationOverride);
		}
	}

	if (const FChaosMovementSettingsOverridesRemover* MovementSettingsOverrideRemover = InputData.InputCmd.InputCollection.FindDataByType<FChaosMovementSettingsOverridesRemover>())
	{
		// If the override contains a mode name find that mode to override. Otherwise take the latest mode,
		// or the pending mode if a new one has been set
		TWeakObjectPtr<UBaseMovementMode> ModePtr = !MovementSettingsOverrideRemover->ModeName.IsNone() ? StateMachine.FindMovementMode_Mutable(MovementSettingsOverrideRemover->ModeName) :
			(CharacterDefaultInputs.SuggestedMovementMode.IsNone() ? StateMachine.GetCurrentMode() : StateMachine.FindMovementMode_Mutable(CharacterDefaultInputs.SuggestedMovementMode));

		if (IChaosCharacterMovementModeInterface* ModeInterface = Cast<IChaosCharacterMovementModeInterface>(ModePtr.Get()))
		{
			ModeInterface->ClearMaxSpeedOverride();
			ModeInterface->ClearAccelerationOverride();
		}
	}

	// The state machine works in local space relative to the ground, so if we're on
	// the ground make the state relative to the ground
	// TODO - Can we use the local space in the sync state rather than subtracting and
	//        re-adding ground velocity to the world space velocity?
	FVector LocalGroundVelocity = FVector::ZeroVector;
	const UBaseMovementMode* Mode = StateMachine.FindMovementMode(CurrentSyncState.MovementMode).Get();
	if (Mode && Mode->HasGameplayTag(Mover_IsOnGround, true))
	{
		FMoverDefaultSyncState& PreSimDefaultSyncState = CurrentSyncState.SyncStateCollection.FindOrAddMutableDataByType<FMoverDefaultSyncState>();

		FFloorCheckResult LastFloorResult;
		if (Blackboard->TryGet(CommonBlackboard::LastFloorResult, LastFloorResult))
		{
			LocalGroundVelocity = FVector::VectorPlaneProject(
				UChaosGroundMovementUtils::ComputeLocalGroundVelocity_Internal(PreSimDefaultSyncState.GetLocation_WorldSpace(), LastFloorResult),
				LastFloorResult.HitResult.ImpactNormal);
			
			if (!LocalGroundVelocity.IsNearlyZero())
			{
				PreSimDefaultSyncState.SetTransforms_WorldSpace(
					PreSimDefaultSyncState.GetLocation_WorldSpace(),
					PreSimDefaultSyncState.GetOrientation_WorldSpace(),
					PreSimDefaultSyncState.GetVelocity_WorldSpace() - LocalGroundVelocity,
					PreSimDefaultSyncState.GetAngularVelocityDegrees_WorldSpace());
			}
		}
	}

	FChaosMoverGroundSimState& GroundSimState = InternalSimData.FindOrAddMutableDataByType<FChaosMoverGroundSimState>();
	GroundSimState.LocalVelocity = LocalGroundVelocity;
}

void UChaosMoverSimulation::OnSimulationRollback(const FMoverTimeStep& NewTimeStep, const FMoverSyncState& PrevSyncState)
{
	// Rollback blackboard on the first frame of resimulation
	Blackboard->Invalidate(EInvalidationReason::Rollback);
	RollbackBlackboard->BeginRollback(NewTimeStep);

	// Remove any events that have been stored for this simulation tick
	Events.Empty();

	// Make sure the movement basis matches the one in the sync state
	if (FChaosMovementBasis* MovementBasis = CurrentSyncState.SyncStateCollection.FindMutableDataByType<FChaosMovementBasis>())
	{
		SetMovementBasisTransform(FTransform(MovementBasis->BasisRotation, MovementBasis->BasisLocation));
	}

	StateMachine.OnSimulationRollback(NewTimeStep, PrevSyncState, CurrentSyncState);
	
	RollbackBlackboard->EndRollback();
}

void UChaosMoverSimulation::OnPreSimulationTick(const FMoverTimeStep& TimeStep, const UE::ChaosMover::FSimulationInputData& InputData)
{
	InternalServerFrame = TimeStep.ServerFrame;

	// Update the sync state from the current physics state
	FMoverDefaultSyncState& PreSimDefaultSyncState = CurrentSyncState.SyncStateCollection.FindOrAddMutableDataByType<FMoverDefaultSyncState>();
	PreSimDefaultSyncState.SetMovementBase(nullptr);
	if (const Chaos::FPBDRigidParticleHandle* ParticleHandle = GetControlledParticle())
	{
		PreSimDefaultSyncState.SetTransforms_WorldSpace(ParticleHandle->GetX(), FRotator(ParticleHandle->GetR()), ParticleHandle->GetV(), FMath::RadiansToDegrees(ParticleHandle->GetW()));
	}

	if (TStrongObjectPtr<UBaseMovementMode> CurrentModePtr = StateMachine.GetCurrentMode().Pin())
	{
		UBaseMovementMode* CurrentMode = CurrentModePtr.Get();
		if (IChaosCharacterMovementModeInterface* CharacterMode = Cast<IChaosCharacterMovementModeInterface>(CurrentMode))
		{
			PreSimulationTickCharacter(*CharacterMode, TimeStep, InputData);
		}
	}
}

void UChaosMoverSimulation::OnSimulationTick(const FMoverTimeStep& TimeStep, const UE::ChaosMover::FSimulationInputData& InputData, UE::ChaosMover::FSimulationOutputData& OutputData)
{
	check(Blackboard.Get());

	FMoverTickStartData TickStartData(InputData.InputCmd, CurrentSyncState, InputData.AuxInputState);
	FMoverTickEndData TickEndData(&CurrentSyncState, &InputData.AuxInputState);

	StateMachine.OnSimulationTick(TimeStep, TickStartData, Blackboard.Get(), MovementMixerWeakPtr.Get(), TickEndData);

	// Copy the sync state locally and to the output data
	OutputData.SyncState = TickEndData.SyncState;
	OutputData.LastUsedInputCmd = InputData.InputCmd;
}

void UChaosMoverSimulation::PostSimulationTickCharacter(const IChaosCharacterMovementModeInterface& CharacterMode, const FMoverTimeStep& TimeStep, UE::ChaosMover::FSimulationOutputData& OutputData)
{
	FChaosMoverCharacterSimState& CharacterSimState = InternalSimData.FindOrAddMutableDataByType<FChaosMoverCharacterSimState>();
	FMoverDefaultSyncState* PostSimDefaultSyncState = OutputData.SyncState.SyncStateCollection.FindMutableDataByType<FMoverDefaultSyncState>();
	check(PostSimDefaultSyncState);

	if (Chaos::FPBDRigidParticleHandle* ParticleHandle = GetControlledParticle())
	{
		// Linear motion
		CharacterSimState.TargetDeltaPosition = PostSimDefaultSyncState->GetLocation_WorldSpace() - ParticleHandle->GetX();

		FVector LocalGroundVelocity = FVector::ZeroVector;
		if (const FChaosMoverGroundSimState* GroundSimState = InternalSimData.FindDataByType<FChaosMoverGroundSimState>())
		{
			LocalGroundVelocity = GroundSimState->LocalVelocity;
		}
		ParticleHandle->SetV(PostSimDefaultSyncState->GetVelocity_WorldSpace() + LocalGroundVelocity);

		// Angular motion
		FQuat TgtQuat = PostSimDefaultSyncState->GetOrientation_WorldSpace().Quaternion();
		TgtQuat.EnforceShortestArcWith(ParticleHandle->GetR());
		FQuat QuatRotation = TgtQuat * ParticleHandle->GetR().Inverse();
		FVector AngularDisplacement = QuatRotation.ToRotationVector();

		FVector UpDir = FVector::UpVector;
		if (const FChaosMoverSimulationDefaultInputs* SimInputs = LocalSimInput.FindDataByType<FChaosMoverSimulationDefaultInputs>())
		{
			UpDir = SimInputs->UpDir;
		}
		CharacterSimState.TargetDeltaFacing = AngularDisplacement.Dot(UpDir);

		if (CharacterMode.ShouldCharacterRemainUpright())
		{
			const float DeltaTimeSeconds = TimeStep.StepMs * 0.001f;
			if (DeltaTimeSeconds > UE_SMALL_NUMBER)
			{
				ParticleHandle->SetW(AngularDisplacement / DeltaTimeSeconds);
			}
		}
	}
	else
	{
		CharacterSimState.TargetDeltaPosition = FVector::ZeroVector;
		CharacterSimState.TargetDeltaFacing = 0.0f;
	}

	// Update the movement base
	check(Blackboard.Get());
	FFloorCheckResult FloorResult;
	bool FoundLastFloorResult = Blackboard->TryGet(CommonBlackboard::LastFloorResult, FloorResult);
	bool FoundFloor = FoundLastFloorResult && FloorResult.bBlockingHit;
	// Note: We want to record the movement base but we don't record the transform
	// so don't use this to get a relative transform for the sync state
	PostSimDefaultSyncState->SetMovementBase(FoundFloor ? FloorResult.HitResult.GetComponent() : nullptr);

	FFloorResultData& FloorData = OutputData.AdditionalOutputData.FindOrAddMutableDataByType<FFloorResultData>();
	FloorData.FloorResult = FloorResult;
}

void UChaosMoverSimulation::PostSimulationTickCharacterConstraint(const IChaosCharacterConstraintMovementModeInterface& CharacterConstraintMode, const FMoverTimeStep& TimeStep, UE::ChaosMover::FSimulationOutputData& OutputData)
{
	if (!CharacterConstraintHandle)
	{
		return;
	}

	if (CharacterConstraintMode.ShouldEnableConstraint() && !CharacterConstraintHandle->IsEnabled())
	{
		EnableCharacterConstraint();
	}
	else if (!CharacterConstraintMode.ShouldEnableConstraint() && CharacterConstraintHandle->IsEnabled())
	{
		DisableCharacterConstraint();
		return;
	}

	// Update the up direction in the settings
	Chaos::FCharacterGroundConstraintSettings& Settings = CharacterConstraintHandle->GetSettings_Mutable();
	if (const FChaosMoverSimulationDefaultInputs* SimInputs = LocalSimInput.FindDataByType<FChaosMoverSimulationDefaultInputs>())
	{
		Settings.VerticalAxis = SimInputs->UpDir;
		Settings.TargetHeight = CharacterConstraintMode.GetTargetHeight();
	}

	check(Blackboard.Get());

	FFloorCheckResult FloorResult;

	// Update the constraint data based on the floor result
	if (Blackboard->TryGet(CommonBlackboard::LastFloorResult, FloorResult) && FloorResult.bBlockingHit)
	{
		// Set the ground particle on the constraint
		Chaos::FGeometryParticleHandle* GroundParticle = nullptr;

		if (Chaos::FPhysicsObjectHandle GroundPhysicsObject = FloorResult.HitResult.PhysicsObject)
		{
			Chaos::FReadPhysicsObjectInterface_Internal ReadInterface = Chaos::FPhysicsObjectInternalInterface::GetRead();
			if (!ReadInterface.AreAllDisabled({ GroundPhysicsObject }))
			{
				GroundParticle = ReadInterface.GetParticle(GroundPhysicsObject);
				if (ReadInterface.AreAllSleeping({ GroundPhysicsObject }))
				{
					Chaos::FWritePhysicsObjectInterface_Internal WriteInterface = Chaos::FPhysicsObjectInternalInterface::GetWrite();
					WriteInterface.WakeUp({ GroundPhysicsObject });
				}
			}
		}
		CharacterConstraintHandle->SetGroundParticle(GroundParticle);

		// Set the max walkable slope angle using any override from the hit component
		float WalkableSlopeCosine = CharacterConstraintHandle->GetSettings().CosMaxWalkableSlopeAngle;
		if (const TStrongObjectPtr<UPrimitiveComponent> PrimComp = FloorResult.HitResult.Component.Pin())
		{
			const FWalkableSlopeOverride& SlopeOverride = PrimComp->GetWalkableSlopeOverride();
			WalkableSlopeCosine = SlopeOverride.ModifyWalkableFloorZ(WalkableSlopeCosine);
		}

		if (!FloorResult.bWalkableFloor)
		{
			WalkableSlopeCosine = 2.0f;
		}

		FChaosMoverCharacterSimState* CharacterSimState = InternalSimData.FindMutableDataByType<FChaosMoverCharacterSimState>();
		check(CharacterSimState);

		CharacterConstraintHandle->SetData({
			FloorResult.HitResult.ImpactNormal,
			CharacterSimState->TargetDeltaPosition,
			CharacterSimState->TargetDeltaFacing,
			FloorResult.FloorDist,
			WalkableSlopeCosine
			});
	}
	else
	{
		CharacterConstraintHandle->SetGroundParticle(nullptr);
		CharacterConstraintHandle->SetData({
			CharacterConstraintHandle->GetSettings().VerticalAxis,
			Chaos::FVec3::ZeroVector,
			0.0,
			1.0e10,
			0.5f
			});
	}
}

void UChaosMoverSimulation::PostSimulationTickMovementActuation(const IChaosMovementActuationInterface& MovementActuationMode, const FMoverTimeStep& TimeStep, UE::ChaosMover::FSimulationOutputData& OutputData)
{
	if (!ActuationConstraintHandle)
	{
		return;
	}

	if (MovementActuationMode.ShouldUseConstraint())
	{
		if (!ActuationConstraintHandle->IsEnabled())
		{
			EnableActuationConstraint();
		}

		if (!IsControlledParticleDynamic())
		{
			SetControlledParticleDynamic();
		}

		ActuationConstraintHandle->SetSettings(MovementActuationMode.GetConstraintSettings());
	}
	else if (!MovementActuationMode.ShouldUseConstraint())
	{
		if (ActuationConstraintHandle->IsEnabled())
		{
			DisableActuationConstraint();
		}

		if (!IsControlledParticleKinematic())
		{
			SetControlledParticleKinematic();
		}
	}

	const FMoverDefaultSyncState* PostSimDefaultSyncState = OutputData.SyncState.SyncStateCollection.FindMutableDataByType<FMoverDefaultSyncState>();
	check(PostSimDefaultSyncState);
	SetActuationTargetTransform(PostSimDefaultSyncState->GetTransform_WorldSpace());
}

void UChaosMoverSimulation::SetActuationTargetTransform(const FTransform& TargetTransform)
{
	Chaos::FPBDRigidsEvolution* Evolution = Solver ? Solver->GetEvolution() : nullptr;
	if (!Evolution)
	{
		return;
	}

	Chaos::FKinematicTarget KinematicPathTarget = Chaos::FKinematicTarget::MakePositionTarget(TargetTransform);
	if (ActuationConstraintEndPointParticleHandle)
	{	
		Evolution->SetParticleKinematicTarget(ActuationConstraintEndPointParticleHandle, KinematicPathTarget);
	}
	if (IsControlledParticleKinematic())
	{
		if (Chaos::FPBDRigidParticleHandle* ParticleHandle = GetControlledParticle())
		{
			Evolution->SetParticleKinematicTarget(ParticleHandle, KinematicPathTarget);
		}
	}
}

void UChaosMoverSimulation::TeleportActuationTarget(const FTransform& TargetTransform, bool AlsoTeleportControlledParticle /*= false*/)
{
	Chaos::FPBDRigidsEvolution* Evolution = Solver ? Solver->GetEvolution() : nullptr;
	if (!Evolution)
	{
		return;
	}

	if (ActuationConstraintEndPointParticleHandle)
	{	
		Evolution->SetParticleTransform(ActuationConstraintEndPointParticleHandle, TargetTransform.GetTranslation(), TargetTransform.GetRotation(), /*bIsTeleport=*/true);
	}
	if (AlsoTeleportControlledParticle)
	{
		if (Chaos::FPBDRigidParticleHandle* ParticleHandle = GetControlledParticle())
		{
			Evolution->SetParticleTransform(ParticleHandle, TargetTransform.GetTranslation(), TargetTransform.GetRotation(), /*bIsTeleport=*/true);
		}
	}
}

const FTransform& UChaosMoverSimulation::GetMovementBasisTransform() const
{
	return MovementBasisTransform;
}

void UChaosMoverSimulation::SetMovementBasisTransform(const FTransform& InMovementBasisTransform)
{
	MovementBasisTransform = InMovementBasisTransform;
}

bool UChaosMoverSimulation::CanTeleport(const FTransform& TargetTransform, bool bUseActorRotation, const FMoverSyncState& SyncState)
{
	Chaos::FPBDRigidsEvolution* Evolution = Solver ? Solver->GetEvolution() : nullptr;
	Chaos::FPBDRigidParticleHandle* ParticleHandle = GetControlledParticle();
	if (!Evolution || !ParticleHandle)
	{
		return false;
	}

	// TODO: Add flags to modes for a teleport policy:
	// - Does the object fit or encroach? What shape type to use, or AABB, OBB?
	// - Is the floor underneath walkable?
	// - ... or should the mode be directly in charge of deciding whether one can teleport or not?
	const FMoverDefaultSyncState* DefaultSyncState = SyncState.SyncStateCollection.FindDataByType<FMoverDefaultSyncState>();
	if (!DefaultSyncState)
	{
		return false;
	}
	FTransform FinalTargetTransform(bUseActorRotation ? FQuat(DefaultSyncState->GetOrientation_WorldSpace()) : TargetTransform.GetRotation(), TargetTransform.GetLocation());
	TStrongObjectPtr<UBaseMovementMode> CurrentModePtr = StateMachine.GetCurrentMode().Pin();
	UChaosMovementMode* ChaosMovementMode = Cast<UChaosMovementMode>(CurrentModePtr.Get());
	return !ChaosMovementMode || ChaosMovementMode->CanTeleport(FinalTargetTransform, CurrentSyncState);
}

void UChaosMoverSimulation::Teleport(const FTransform& TargetTransform, const FMoverTimeStep& TimeStep, FMoverSyncState& OutputState)
{
	Chaos::FPBDRigidsEvolution* Evolution = Solver ? Solver->GetEvolution() : nullptr;
	Chaos::FPBDRigidParticleHandle* ParticleHandle = GetControlledParticle();
	if (!Evolution || !ParticleHandle)
	{
		return;
	}

	FMoverDefaultSyncState& DefaultSyncState = OutputState.SyncStateCollection.FindOrAddMutableDataByType<FMoverDefaultSyncState>();
	FTransform CurrentTransform(FQuat(DefaultSyncState.GetOrientation_WorldSpace()), DefaultSyncState.GetLocation_WorldSpace());
	FTransform TeleportDeltaTransform = CurrentTransform.Inverse() * TargetTransform;
	
	TStrongObjectPtr<UBaseMovementMode> CurrentModePtr = StateMachine.GetCurrentMode().Pin();
	UChaosMovementMode* ChaosMovementMode = Cast<UChaosMovementMode>(CurrentModePtr.Get());

	// Teleport the movement basis transform if the current mode uses one. Should we do this if ANY mode on the state machine uses a movement basis transform?
	if (ChaosMovementMode && ChaosMovementMode->UsesMovementBasisTransform())
	{
		// Transform that changes CurrentParticleTransform into TargetTransform
		FTransform NewMovementBasisTransform = MovementBasisTransform * TeleportDeltaTransform;
		SetMovementBasisTransform(NewMovementBasisTransform);
		// Add and/or update the movement basis transform as part of our state
		// For this to lead to a change of movement basis transform for sim proxies, we either need movement replication to be on
		// or, for an actor moved kinematically, "Apply Sim Proxy State at Runtime" to be checked in the network settings component
		FChaosMovementBasis& MovementBasis = OutputState.SyncStateCollection.FindOrAddMutableDataByType<FChaosMovementBasis>();
		MovementBasis.BasisLocation = NewMovementBasisTransform.GetLocation();
		MovementBasis.BasisRotation = NewMovementBasisTransform.GetRotation();
	}

	// Teleport the actuation target
	if (IsActuationConstraintEnabled())
	{
		TeleportActuationTarget(TargetTransform, /* AlsoTeleportControlledParticle = */ false); // We teleport the particle separately below
	}

	// Completely disable the character constraint and go into default mode
	// TODO: reestablish the constraint if ground is found at the target transform
	// This might not be necessary, the character ground constraint should be teleport friendly
	if (IsCharacterConstraintEnabled())
	{
		DisableCharacterConstraint();
		StateMachine.SetModeImmediately(StateMachine.GetDefaultModeName());		
	}

	// Invalidate floor results. Should there be a way to indicate which blackboard results to invalidate for which type of operation, like rewind, teleport, etc?
	Blackboard->Invalidate(CommonBlackboard::LastFloorResult);
	Blackboard->Invalidate(CommonBlackboard::LastFoundDynamicMovementBase);

	// Rotate the velocities by the teleport rotation
	FVector NewLinearVelocity = TeleportDeltaTransform.TransformVector(ParticleHandle->GetV());
	FVector NewAngularVelocity = TeleportDeltaTransform.TransformVector(ParticleHandle->GetW());

	// Teleport the particle
	//     - Disable it
	Evolution->DisableParticle(ParticleHandle);
	//	   - Place the particle at the target transform
	Evolution->SetParticleTransform(ParticleHandle, TargetTransform.GetTranslation(), TargetTransform.GetRotation(), /*bIsTeleport=*/true);
	Evolution->SetParticleVelocities(ParticleHandle, NewLinearVelocity, NewAngularVelocity);
	//	   - Re-enable the particle
	Evolution->EnableParticle(ParticleHandle);

	// Invalidate the movement base for now, until we reestablish the character ground constraint
	DefaultSyncState.SetMovementBase(nullptr);
	// Make sure the sync state matches the particle transforms
	DefaultSyncState.SetTransforms_WorldSpace(ParticleHandle->GetX(), FRotator(ParticleHandle->GetR()), ParticleHandle->GetV(), FMath::RadiansToDegrees(ParticleHandle->GetW()));
	// Rotate the move direction intent
	DefaultSyncState.MoveDirectionIntent = TeleportDeltaTransform.TransformVector(DefaultSyncState.MoveDirectionIntent);
}

void UChaosMoverSimulation::OnPostSimulationTick(const FMoverTimeStep& TimeStep, UE::ChaosMover::FSimulationOutputData& OutputData)
{
	// TODO - make this more extensible
	if (TStrongObjectPtr<UBaseMovementMode> CurrentModePtr = StateMachine.GetCurrentMode().Pin())
	{
		if (IChaosCharacterMovementModeInterface* CharacterMode = Cast<IChaosCharacterMovementModeInterface>(CurrentModePtr.Get()))
		{
			PostSimulationTickCharacter(*CharacterMode, TimeStep, OutputData);
		}

		if (IChaosCharacterConstraintMovementModeInterface* CharacterConstraintMode = Cast<IChaosCharacterConstraintMovementModeInterface>(CurrentModePtr.Get()))
		{
			PostSimulationTickCharacterConstraint(*CharacterConstraintMode, TimeStep, OutputData);
		}
		else
		{
			DisableCharacterConstraint();
		}

		if (IChaosMovementActuationInterface* MovementActuationMode = Cast<IChaosMovementActuationInterface>(CurrentModePtr.Get()))
		{
			PostSimulationTickMovementActuation(*MovementActuationMode, TimeStep, OutputData);
		}
		else
		{
			DisableActuationConstraint();
		}
	}

	CurrentSyncState = OutputData.SyncState;

	// Extract the events into the output data and clear
	OutputData.Events = Events;
	Events.Empty();

	// Send Debug Data to the Chaos Visual Debugger
	TraceMoverData(TimeStep, OutputData);
}

void UChaosMoverSimulation::TraceMoverData(const FMoverTimeStep& TimeStep, const UE::ChaosMover::FSimulationOutputData& OutputData)
{
	// Send the latest physics thread data to CVD
#if WITH_CHAOS_VISUAL_DEBUGGER
	if (FChaosVisualDebuggerTrace::IsTracing())
	{
		static FName NAME_LocalSimImput("LocalSimImput");
		static FName NAME_InternalSimData("InternalSimData");
		static FName NAME_DebugSimData("DebugSimData");

		FChaosMoverTimeStepDebugData& TimeStepDebugData = DebugSimData.FindOrAddMutableDataByType<FChaosMoverTimeStepDebugData>();
		TimeStepDebugData.SetTimeStep(TimeStep);

		UE::MoverUtils::NamedDataCollections LocalSimDataCollections (
			{				
				{ NAME_LocalSimImput, &LocalSimInput},
				{ NAME_InternalSimData, &InternalSimData},
				{ NAME_DebugSimData, &DebugSimData}
			});

		Chaos::FReadPhysicsObjectInterface_Internal Interface = Chaos::FPhysicsObjectInternalInterface::GetRead();
		const Chaos::FGeometryParticleHandle* ParticleHandle = PhysicsObject? Interface.GetParticle(PhysicsObject) : nullptr;
		int32 ParticleID = ParticleHandle ? ParticleHandle->UniqueIdx().Idx : INDEX_NONE;

		int32 SolverID = CVD_TRACE_GET_SOLVER_ID_FROM_WORLD(World);

		UE::MoverUtils::FMoverCVDRuntimeTrace::TraceMoverData(SolverID, ParticleID, &OutputData.LastUsedInputCmd, &OutputData.SyncState, &LocalSimDataCollections);

		// Clear debug sim data to avoid recording past debug data as current on the next frame
		DebugSimData.Empty();
	}
#endif
}

void UChaosMoverSimulation::OnModifyContacts(const FMoverTimeStep& TimeStep, const UE::ChaosMover::FSimulationInputData& InputData, const UE::ChaosMover::FSimulationOutputData& OutputData, Chaos::FCollisionContactModifier& Modifier)
{
}

void UChaosMoverSimulation::AddEvent(TSharedPtr<FMoverSimulationEventData> Event)
{
	// Events are added to the event list for later extraction to game thread
	// We also allow the simulation to react to the event immediately
	Events.Add(Event);

	if (const FMoverSimulationEventData* EventData = Event.Get())
	{
		ProcessSimulationEvent(*EventData);
	}
}

void UChaosMoverSimulation::ProcessSimulationEvent(const FMoverSimulationEventData& EventData)
{
	if (const FMovementModeChangedEventData* ModeChangedEventData = EventData.CastTo<FMovementModeChangedEventData>())
	{
		OnMovementModeChanged(*ModeChangedEventData);
	}
}

void UChaosMoverSimulation::OnMovementModeChanged(const FMovementModeChangedEventData& ModeChangedData)
{
	TStrongObjectPtr<UBaseMovementMode> PreviousModePtr = StateMachine.FindMovementMode(ModeChangedData.PreviousModeName).Pin();
	TStrongObjectPtr<UBaseMovementMode> NextModePtr = StateMachine.FindMovementMode(ModeChangedData.NewModeName).Pin();

	if (PreviousModePtr && NextModePtr)
	{
		if (IChaosCharacterConstraintMovementModeInterface* NextCharacterConstraintMode = Cast<IChaosCharacterConstraintMovementModeInterface>(NextModePtr.Get()))
		{
			if (CharacterConstraintHandle)
			{
				Chaos::FCharacterGroundConstraintSettings& Settings = CharacterConstraintHandle->GetSettings_Mutable();
				NextCharacterConstraintMode->UpdateConstraintSettings(Settings);

				// Character ground constraint modes currently assume moving a dynamic particle and using a character ground constraint
				// Revise if we start supporting moving a character kinematically
				if (!IsControlledParticleDynamic())
				{
					SetControlledParticleDynamic();
				}
			}
		}

		if (IChaosMovementActuationInterface* NextMovementActuationInterface = Cast<IChaosMovementActuationInterface>(NextModePtr.Get()))
		{
			// Reset the pathed movement state
			FChaosPathedMovementState* PresimPathedMovementState = CurrentSyncState.SyncStateCollection.FindMutableDataByType<FChaosPathedMovementState>();
			if (PresimPathedMovementState)
			{
				*PresimPathedMovementState = FChaosPathedMovementState();
			}

			if (ActuationConstraintHandle)
			{
				if (NextMovementActuationInterface->ShouldUseConstraint())
				{
					// Enable the constraint
					if (!ActuationConstraintHandle->IsEnabled())
					{
						EnableActuationConstraint();
					}
					// Set the controlled particle dynamic
					if (!IsControlledParticleDynamic())
					{
						SetControlledParticleDynamic();
					}
					// Apply movement actuation constraint settings
					// Bug: if we didn't also call SetSettings every frame in UChaosMoverSimulation::PostSimulationTickMovementActuation,
					// the settings wouldn't apply correctly from this call only if this mode is the first active mode (OnMovementModeChanged called on the very first OnSimulationTick)
					ActuationConstraintHandle->SetSettings(NextMovementActuationInterface->GetConstraintSettings());

					// If the mode was also a pathed mode, let's teleport to the position on the path
					// we're supposed to be at, using CurrentPathProgress
					IChaosPathedMovementModeInterface* NextPathedMovementMode = Cast<IChaosPathedMovementModeInterface>(NextMovementActuationInterface);
					if (NextPathedMovementMode)
					{	
						const float PathProgress = PresimPathedMovementState ? PresimPathedMovementState->LastChangePathProgress : 0.0f;
						// We resume the path at the reset progress
						// (if not 0, CurrentPathProgress was either not reset properly when the movement mode changed, or deliberately set to a different value)
						FTransform TargetLastFrame = NextPathedMovementMode->CalcTargetTransform(PathProgress, MovementBasisTransform);
						TeleportActuationTarget(TargetLastFrame);
					}
				}
				else
				{
					if (ActuationConstraintHandle->IsEnabled())
					{
						DisableActuationConstraint();
					}
					if (!IsControlledParticleKinematic())
					{
						SetControlledParticleKinematic();
					}
				}
			}
		}
	}
}

Chaos::FPBDRigidParticleHandle* UChaosMoverSimulation::GetControlledParticle() const
{
	if (PhysicsObject)
	{
		Chaos::FReadPhysicsObjectInterface_Internal Interface = Chaos::FPhysicsObjectInternalInterface::GetRead();
		return Interface.GetRigidParticle(PhysicsObject);
	}
	return nullptr;
}

void UChaosMoverSimulation::SetControlledParticleDynamic()
{
	if (Chaos::FPBDRigidParticleHandle* ControlledParticle = GetControlledParticle())
	{
		check(Solver);
		Chaos::FPBDRigidsEvolution& Evolution = *Solver->GetEvolution();
		Evolution.SetParticleObjectState(ControlledParticle, Chaos::EObjectStateType::Dynamic);
	}
}

void UChaosMoverSimulation::SetControlledParticleKinematic()
{
	if (Chaos::FPBDRigidParticleHandle* ControlledParticle = GetControlledParticle())
	{
		check(Solver);
		Chaos::FPBDRigidsEvolution& Evolution = *Solver->GetEvolution();
		Evolution.SetParticleObjectState(ControlledParticle, Chaos::EObjectStateType::Kinematic);

		if (const Chaos::TPBDRigidParticleHandleImp<Chaos::FReal, 3, true>* ControlledRigidParticle = ControlledParticle->CastToRigidParticle())
		{
			if (ControlledRigidParticle->UpdateKinematicFromSimulation() == true)
			{
				// Should we instead call SetUpdateKinematicFromSimulation on the GT when some of the modes may animate kinematically?
				UE_LOG(LogChaosMover, Warning, TEXT("The updated component for %s is not set to Update Kinematic from Simulation but is being moved kinematically"), *GetClass()->GetName());
			}
		}
	}
}

bool UChaosMoverSimulation::IsControlledParticleDynamic() const
{
	const Chaos::FPBDRigidParticleHandle* ControlledParticle = GetControlledParticle();
	return ControlledParticle ? ControlledParticle->IsDynamic() : false;
}

bool UChaosMoverSimulation::IsControlledParticleKinematic() const
{
	const Chaos::FPBDRigidParticleHandle* ControlledParticle = GetControlledParticle();
	return ControlledParticle ? ControlledParticle->IsKinematic() : false;
}

void UChaosMoverSimulation::EnableCharacterConstraint()
{
	if (CharacterConstraintHandle)
	{
		if (CharacterConstraintHandle->GetCharacterParticle())
		{
			CharacterConstraintHandle->SetEnabled(true);
		}
	}
}

void UChaosMoverSimulation::DisableCharacterConstraint()
{
	if (CharacterConstraintHandle)
	{
		CharacterConstraintHandle->SetEnabled(false);
	}
}

bool UChaosMoverSimulation::IsCharacterConstraintEnabled() const
{
	return (CharacterConstraintHandle && CharacterConstraintHandle->IsEnabled());
}

void UChaosMoverSimulation::EnableActuationConstraint()
{
	if (ActuationConstraintHandle)
	{
		ActuationConstraintHandle->SetConstraintEnabled(true);
	}
}

void UChaosMoverSimulation::DisableActuationConstraint()
{

	if (ActuationConstraintHandle)
	{
		ActuationConstraintHandle->SetConstraintEnabled(false);
	}
}

bool UChaosMoverSimulation::IsActuationConstraintEnabled() const
{
	return (ActuationConstraintHandle && ActuationConstraintHandle->IsEnabled());
}

void UChaosMoverSimulation::K2_QueueInstantMovementEffect(const int32& EffectAsRawData)
{
	// This will never be called, the exec version below will be hit instead
	checkNoEntry();
}

DEFINE_FUNCTION(UChaosMoverSimulation::execK2_QueueInstantMovementEffect)
{
	Stack.StepCompiledIn<FStructProperty>(nullptr);
	void* EffectPtr = Stack.MostRecentPropertyAddress;
	FStructProperty* StructProp = CastField<FStructProperty>(Stack.MostRecentProperty);

	P_FINISH;

	P_NATIVE_BEGIN;

	const bool bHasValidStructProp = StructProp && StructProp->Struct && StructProp->Struct->IsChildOf(FInstantMovementEffect::StaticStruct());

	if (ensureMsgf((bHasValidStructProp && EffectPtr), TEXT("An invalid type (%s) was sent to a QueueInstantMovementEffect node. A struct derived from FInstantMovementEffect is required. No Movement Effect will be queued."),
		StructProp ? *GetNameSafe(StructProp->Struct) : *Stack.MostRecentProperty->GetClass()->GetName()))
	{
		// Could we steal this instead of cloning? (move semantics)
		FInstantMovementEffect* EffectAsBasePtr = reinterpret_cast<FInstantMovementEffect*>(EffectPtr);
		FInstantMovementEffect* ClonedMove = EffectAsBasePtr->Clone();

		P_THIS->QueueInstantMovementEffect(TSharedPtr<FInstantMovementEffect>(ClonedMove), /*bShouldRollBack =*/ true); // Physics Thread emitted effects can be rolled back because we expect resim to rerun all Physics Thread logic
	}

	P_NATIVE_END;
}

void UChaosMoverSimulation::QueueInstantMovementEffect(TSharedPtr<FInstantMovementEffect> InstantMovementEffect, bool bShouldRollBack)
{
	// We always use a server frame to schedule, never a time. This is because ChaosMover is always in async mode
    // (if we're not, UChaosMoverBackendComponent::InitializeComponent should have warned us to fix that)
	QueueInstantMovementEffect(FScheduledInstantMovementEffect{ InternalServerFrame, /* ExecutionDateSeconds = */ 0.0, /* bIsAsyncMode = */ true, InstantMovementEffect }, bShouldRollBack);
}

void UChaosMoverSimulation::QueueInstantMovementEffect(const FScheduledInstantMovementEffect& ScheduledInstantMovementEffect, bool bShouldRollBack)
{
	StateMachine.QueueInstantMovementEffect(FChaosScheduledInstantMovementEffect{ InternalServerFrame, bShouldRollBack, ScheduledInstantMovementEffect });
}

DEFINE_FUNCTION(UChaosMoverSimulation::execK2_QueueMovementModifier)
{
	Stack.StepCompiledIn<FStructProperty>(nullptr);
	void* MovePtr = Stack.MostRecentPropertyAddress;
	FStructProperty* StructProp = CastField<FStructProperty>(Stack.MostRecentProperty);

	P_FINISH;

	P_NATIVE_BEGIN;

	const bool bHasValidStructProp = StructProp && StructProp->Struct && StructProp->Struct->IsChildOf(FMovementModifierBase::StaticStruct());

	if (ensureMsgf((bHasValidStructProp && MovePtr), TEXT("An invalid type (%s) was sent to a QueueMovementModifier node. A struct derived from FMovementModifierBase is required. No modifier will be queued."),
		StructProp ? *GetNameSafe(StructProp->Struct) : *Stack.MostRecentProperty->GetClass()->GetName()))
	{
		// Could we steal this instead of cloning? (move semantics)
		FMovementModifierBase* MoveAsBasePtr = reinterpret_cast<FMovementModifierBase*>(MovePtr);
		FMovementModifierBase* ClonedMove = MoveAsBasePtr->Clone();

		FMovementModifierHandle ModifierID = P_THIS->QueueMovementModifier(TSharedPtr<FMovementModifierBase>(ClonedMove));
		*static_cast<FMovementModifierHandle*>(RESULT_PARAM) = ModifierID;
	}

	P_NATIVE_END;
}

FMovementModifierHandle UChaosMoverSimulation::QueueMovementModifier(TSharedPtr<FMovementModifierBase> Modifier)
{
	return StateMachine.QueueMovementModifier(Modifier);
}

void UChaosMoverSimulation::CancelModifierFromHandle(FMovementModifierHandle ModifierHandle)
{
	StateMachine.CancelModifierFromHandle(ModifierHandle);
}

bool UChaosMoverSimulation::HasGameplayTag(FGameplayTag TagToFind, bool bExactMatch) const
{
	// Check active Movement Mode
	if (const UBaseMovementMode* ActiveMovementMode = FindMovementModeByName(CurrentSyncState.MovementMode))
	{
		if (ActiveMovementMode->HasGameplayTag(TagToFind, bExactMatch))
		{
			return true;
		}
	}

	// Search Movement Modifiers
	for (auto ModifierFromSyncStateIt = CurrentSyncState.MovementModifiers.GetActiveModifiersIterator(); ModifierFromSyncStateIt; ++ModifierFromSyncStateIt)
	{
		if (const TSharedPtr<FMovementModifierBase> ModifierFromSyncState = *ModifierFromSyncStateIt)
		{
			if (ModifierFromSyncState.IsValid() && ModifierFromSyncState->HasGameplayTag(TagToFind, bExactMatch))
			{
				return true;
			}
		}
	}

	// Search Layered Moves
	for (const TSharedPtr<FLayeredMoveBase>& LayeredMove : CurrentSyncState.LayeredMoves.GetActiveMoves())
	{
		if (LayeredMove->HasGameplayTag(TagToFind, bExactMatch))
		{
			return true;
		}
	}

	return false;
}

const FMovementModifierBase* UChaosMoverSimulation::FindMovementModifierByType(const UScriptStruct* DataStructType) const
{
	// Check active modifiers for modifier handle
	for (auto ActiveModifierFromSyncStateIt = CurrentSyncState.MovementModifiers.GetActiveModifiersIterator(); ActiveModifierFromSyncStateIt; ++ActiveModifierFromSyncStateIt)
	{
		const TSharedPtr<FMovementModifierBase> ActiveModifierFromSyncState = *ActiveModifierFromSyncStateIt;

		if (DataStructType == ActiveModifierFromSyncState->GetScriptStruct())
		{
			return ActiveModifierFromSyncState.Get();
		}
	}

	return StateMachine.FindQueuedModifierByType(DataStructType);
}

void UChaosMoverSimulation::AttemptTeleport(const FMoverTimeStep& TimeStep, const FTransform& TargetTransform, bool bUseActorRotation, FMoverSyncState& OutputState)
{
	const FMoverDefaultSyncState* DefaultSyncState = OutputState.SyncStateCollection.FindDataByType<FMoverDefaultSyncState>();

	FTransform FromTransform = DefaultSyncState ? DefaultSyncState->GetTransform_WorldSpace() : FTransform::Identity;
	const FVector& ToLocation = TargetTransform.GetLocation();
	FQuat ToRotation = (DefaultSyncState && bUseActorRotation) ? FromTransform.GetRotation() : TargetTransform.GetRotation();
	FTransform TeleportTransform(ToRotation, TargetTransform.GetLocation());
	if (CanTeleport(TeleportTransform, false, OutputState))
	{
		// Then we need to teleport
		Teleport(TeleportTransform, TimeStep, OutputState);

		AddEvent(MakeShared<FTeleportSucceededEventData>(TimeStep.BaseSimTimeMs, FromTransform.GetLocation(), FromTransform.GetRotation(), ToLocation, ToRotation));

#if !defined(BUILD_SHIPPING) || !BUILD_SHIPPING
		const FChaosMoverSimulationDefaultInputs* SimInputs = LocalSimInput.FindDataByType<FChaosMoverSimulationDefaultInputs>();
		if (SimInputs && SimInputs->OwningActor)
		{
			UE_LOG(LogChaosMover, Log, TEXT("%s: Actor %s teleported to %s, %s, teleport frame = %d)"),
			*ToString(SimInputs->OwningActor->GetNetMode()), *GetNameSafe(SimInputs->OwningActor),
			*ToLocation.ToString(), *FRotator(ToRotation).ToString(), TimeStep.ServerFrame);
		}
#endif // !defined(BUILD_SHIPPING) || !BUILD_SHIPPING
	}
	else
	{
		ETeleportFailureReason FailureReason = ETeleportFailureReason::Reason_NotAvailable;
		AddEvent(MakeShared<FTeleportFailedEventData>(TimeStep.BaseSimTimeMs, FromTransform.GetLocation(), FromTransform.GetRotation(), ToLocation, ToRotation, FailureReason));

#if !defined(BUILD_SHIPPING) || !BUILD_SHIPPING
		const FChaosMoverSimulationDefaultInputs* SimInputs = LocalSimInput.FindDataByType<FChaosMoverSimulationDefaultInputs>();
		if (SimInputs && SimInputs->OwningActor)
		{
			UE_LOG(LogChaosMover, Log, TEXT("%s: Actor %s could NOT teleport to %s, %s, teleport frame = %d. Reason = %s"),
			*GetNameSafe(SimInputs->OwningActor), *ToString(SimInputs->OwningActor->GetNetMode()),
			*ToLocation.ToString(), *FRotator(ToRotation).ToString(), TimeStep.ServerFrame,
			*StaticEnum<ETeleportFailureReason>()->GetNameStringByValue(static_cast<int64>(FailureReason)));
		}
#endif // !defined(BUILD_SHIPPING) || !BUILD_SHIPPING
	}
}

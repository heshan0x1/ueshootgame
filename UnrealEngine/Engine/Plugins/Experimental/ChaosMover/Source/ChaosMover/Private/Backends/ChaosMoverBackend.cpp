// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosMover/Backends/ChaosMoverBackend.h"

#include "Backends/ChaosMoverSubsystem.h"
#include "ChaosMover/ChaosMoverLog.h"
#include "ChaosMover/ChaosMoverSimulation.h"
#include "Components/PrimitiveComponent.h"
#include "Framework/Threading.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PhysicsVolume.h"
#include "MoveLibrary/MovementMixer.h"
#include "MoveLibrary/MoverBlackboard.h"
#include "MovementModeStateMachine.h"
#include "NetworkChaosMoverData.h"
#include "PBDRigidsSolver.h"
#include "Physics/NetworkPhysicsComponent.h"
#include "PhysicsEngine/PhysicsObjectExternalInterface.h"
#include "PhysicsProxy/CharacterGroundConstraintProxy.h"
#include "PhysicsProxy/JointConstraintProxy.h"
#include "Runtime/Experimental/Chaos/Private/Chaos/PhysicsObjectInternal.h"


#include UE_INLINE_GENERATED_CPP_BY_NAME(ChaosMoverBackend)

UChaosMoverBackendComponent::UChaosMoverBackendComponent()
	: ActuationConstraintPhysicsUserData(&ActuationConstraintInstance)
{
	PrimaryComponentTick.bCanEverTick = false;

	bWantsInitializeComponent = true;
	bAutoActivate = true;

	SimulationClass = UChaosMoverSimulation::StaticClass();

	if (const Chaos::FPhysicsSolver* Solver = GetPhysicsSolver())
	{
		bIsUsingAsyncPhysics = Solver->IsUsingAsyncResults();
	}

	if (Chaos::FPhysicsSolverBase::IsNetworkPhysicsPredictionEnabled())
	{
		SetIsReplicatedByDefault(true);

		// Let's make sure PhysicsReplicationMode is set to Resimulation
		UWorld* World = GetWorld();
		AActor* MyActor = GetOwner();
		if (MyActor && World && World->IsGameWorld() && (World->GetNetMode() != ENetMode::NM_Standalone))
		{
			if (MyActor->GetPhysicsReplicationMode() != EPhysicsReplicationMode::Resimulation)
			{
				MyActor->SetPhysicsReplicationMode(EPhysicsReplicationMode::Resimulation);
				UE_LOG(LogChaosMover, Log, TEXT("ChaosMoverBackend: Setting Physics Replication Mode to Resimulation for %s or movement will not replicate correctly"), *GetNameSafe(MyActor));
			}
		}
	}
}

void UChaosMoverBackendComponent::InitializeComponent()
{
	Super::InitializeComponent();

	if (UWorld* World = GetWorld(); World && World->IsGameWorld())
	{
		Simulation = NewObject<UChaosMoverSimulation>(GetOwner(), SimulationClass, TEXT("ChaosMoverSimulation"), RF_Transient);

		NullMovementMode = NewObject<UNullMovementMode>(&GetMoverComponent(), TEXT("NullMovementMode"), RF_Transient);
		ImmediateModeTransition = NewObject<UImmediateMovementModeTransition>(&GetMoverComponent(), TEXT("ImmediateModeTransition"), RF_Transient);
		TransformOnInit = GetMoverComponent().GetUpdatedComponentTransform();

		Simulation->SetRollbackBlackboard(GetMoverComponent().GetRollbackBlackboard_Internal());

		// Create NetworkPhysicsComponent
		if ((World->GetNetMode() != ENetMode::NM_Standalone) && Chaos::FPhysicsSolverBase::IsNetworkPhysicsPredictionEnabled())
		{
			if (!bIsUsingAsyncPhysics)
			{
				// Verify that the Project Settings have bTickPhysicsAsync turned on.
				// It's easy to waste time forgetting that, since they are off by default.
				UE_LOG(LogChaosMover, Warning, TEXT("Chaos Mover Backend only supports networking with Physics Async. Networked Physics will not work well. Turn on 'Project Settings > Engine - Physics > Tick Physics Async', or play in Standalone Mode"));
				// This is important enough that we break for developers debugging in editor
				UE_DEBUG_BREAK();
			}
			else
			{
				NetworkPhysicsComponent = NewObject<UNetworkPhysicsComponent>(GetOwner(), TEXT("PhysMover_NetworkPhysicsComponent"), RF_Transient);

				// This isn't technically a DSO component, but set it net addressable as though it is
				NetworkPhysicsComponent->SetNetAddressable();
				NetworkPhysicsComponent->SetIsReplicated(true);
				NetworkPhysicsComponent->RegisterComponent();
				if (!NetworkPhysicsComponent->HasBeenInitialized())
				{
					NetworkPhysicsComponent->InitializeComponent();
				}
				NetworkPhysicsComponent->Activate(true);

				// Register network data for recording and rewind/resim
				NetworkPhysicsComponent->CreateDataHistory<UE::ChaosMover::FNetworkDataTraits>(this);

				if (NetworkPhysicsComponent->HasServerWorld())
				{
					if (APawn* PawnOwner = Cast<APawn>(GetOwner()))
					{
						// When we're owned by a pawn, keep an eye on whether it's currently player-controlled or not
						PawnOwner->ReceiveControllerChangedDelegate.AddUniqueDynamic(this, &ThisClass::HandleOwningPawnControllerChanged_Server);
						HandleOwningPawnControllerChanged_Server(PawnOwner, nullptr, PawnOwner->Controller);
					}
					else
					{
						// If the owner isn't a pawn, there's no chance of player input happening, so inputs to the PT are always produced on the server
						NetworkPhysicsComponent->SetIsRelayingLocalInputs(true);
					}
				}
			}
		}
	}
}

void UChaosMoverBackendComponent::UninitializeComponent()
{
	if (NetworkPhysicsComponent)
	{
		NetworkPhysicsComponent->RemoveDataHistory();
		NetworkPhysicsComponent->DestroyComponent();
		NetworkPhysicsComponent = nullptr;
	}

	Super::UninitializeComponent();
}

void UChaosMoverBackendComponent::CreatePhysics()
{
	// Prevent the character particle from sleeping
	Chaos::FPhysicsSolver* Solver = GetPhysicsSolver();
	Chaos::FPBDRigidParticle* ControlledParticle = GetControlledParticle();
	Chaos::FSingleParticlePhysicsProxy* ControlledParticleProxy = ControlledParticle ? static_cast<Chaos::FSingleParticlePhysicsProxy*>(ControlledParticle->GetProxy()) : nullptr;
	if (ControlledParticleProxy)
	{
		ControlledParticle->SetSleepType(Chaos::ESleepType::NeverSleep);
	}

	// Create all possible constraints...
	// ... a character ground constraint, for constraint based character-like movement on ground
	CreateCharacterGroundConstraint();
	// ... a general purpose actuation joint constraint, for example, for constraint based pathed movement
	CreateActuationConstraint();
}

void UChaosMoverBackendComponent::DestroyPhysics()
{
	// Destroy all constraints
	DestroyCharacterGroundConstraint();
	DestroyActuationConstraint();
}

void UChaosMoverBackendComponent::CreateCharacterGroundConstraint()
{
	if (Chaos::FPhysicsSolver* Solver = GetPhysicsSolver())
	{
		if (Chaos::FPBDRigidParticle* ControlledParticle = GetControlledParticle())
		{
			if (Chaos::FSingleParticlePhysicsProxy* ControlledParticleProxy = static_cast<Chaos::FSingleParticlePhysicsProxy*>(ControlledParticle->GetProxy()))
			{
				// Create the character ground constraint, for character-like movement on ground
				CharacterGroundConstraint = MakeUnique<Chaos::FCharacterGroundConstraint>();
				CharacterGroundConstraint->Init(ControlledParticleProxy);
				Solver->RegisterObject(CharacterGroundConstraint.Get());
			}
		}
	}
}

void UChaosMoverBackendComponent::DestroyCharacterGroundConstraint()
{
	if (Chaos::FPhysicsSolver* Solver = GetPhysicsSolver())
	{
		if (CharacterGroundConstraint.IsValid())
		{
			// Note: Proxy gets destroyed when the constraint is deregistered and that deletes the constraint
			Solver->UnregisterObject(CharacterGroundConstraint.Release());
		}
	}
}

void UChaosMoverBackendComponent::CreateActuationConstraint()
{
	const UMoverComponent& MoverComp = GetMoverComponent();
	if (Chaos::FPhysicsObject* PhysicsObject = GetPhysicsObject())
	{
		const FTransform ComponentWorldTransform = MoverComp.GetUpdatedComponent()->GetComponentTransform();
		// Create the constraint via FChaosEngineInterface directly because it allows jointing a "real" object with a point in space (it creates a dummy particle for us)
		FPhysicsConstraintHandle Handle = FChaosEngineInterface::CreateConstraint(PhysicsObject, nullptr, FTransform::Identity, FTransform::Identity);

		bool bIsConstraintValid = false;
		if (Handle.IsValid() && ensure(Handle->IsType(Chaos::EConstraintType::JointConstraintType)))
		{
			if (Chaos::FJointConstraint* Constraint = static_cast<Chaos::FJointConstraint*>(Handle.Constraint))
			{
				// Since we didn't use the ConstraintInstance to actually create the constraint (it requires both bodies exist, see comment above), link everything up manually
				ActuationConstraintHandle = Handle;						
				ActuationConstraintInstance.ConstraintHandle = ActuationConstraintHandle;
				Constraint->SetUserData(&ActuationConstraintPhysicsUserData/*has a (void*)FConstraintInstanceBase*/);
				bIsConstraintValid = true;

				if (Chaos::FPBDRigidParticle* EndpointParticle = Constraint->GetPhysicsBodies()[1]->GetParticle<Chaos::EThreadContext::External>()->CastToRigidParticle())
				{
					EndpointParticle->SetX(ComponentWorldTransform.GetLocation());
					EndpointParticle->SetR(ComponentWorldTransform.GetRotation());
				}
			}
		}

		if (!bIsConstraintValid)
		{
			FChaosEngineInterface::ReleaseConstraint(Handle);
		}
	}
}

void UChaosMoverBackendComponent::DestroyActuationConstraint()
{
	if (ActuationConstraintHandle.IsValid())
	{
		FChaosEngineInterface::ReleaseConstraint(ActuationConstraintHandle);
	}
}

void UChaosMoverBackendComponent::HandleOwningPawnControllerChanged_Server(APawn* OwnerPawn, AController* OldController, AController* NewController)
{
	// Inputs for player-controlled pawns originate on the player's client, all others originate on the server
	if (NetworkPhysicsComponent)
	{
		NetworkPhysicsComponent->SetIsRelayingLocalInputs(!OwnerPawn->IsPlayerControlled());
	}
}

void UChaosMoverBackendComponent::HandleUpdatedComponentPhysicsStateChanged(UPrimitiveComponent* ChangedComponent, EComponentPhysicsStateChange StateChange)
{
	if (StateChange == EComponentPhysicsStateChange::Destroyed)
	{
		bWantsDestroySim = true;
		DestroyPhysics();
	}
	else if (StateChange == EComponentPhysicsStateChange::Created)
	{
		bWantsCreateSim = true;
		CreatePhysics();
	}
}

Chaos::FPhysicsSolver* UChaosMoverBackendComponent::GetPhysicsSolver() const
{
	if (UWorld* World = GetWorld())
	{
		if (FPhysScene* Scene = World->GetPhysicsScene())
		{
			return Scene->GetSolver();
		}
	}
	return nullptr;
}

UMoverComponent& UChaosMoverBackendComponent::GetMoverComponent() const
{
	return *GetOuterUMoverComponent();
}

Chaos::FPhysicsObject* UChaosMoverBackendComponent::GetPhysicsObject() const
{
	IPhysicsComponent* PhysicsComponent = Cast<IPhysicsComponent>(GetMoverComponent().GetUpdatedComponent());
	return PhysicsComponent ? PhysicsComponent->GetPhysicsObjectByName(NAME_None) : nullptr;
}

Chaos::FPBDRigidParticle* UChaosMoverBackendComponent::GetControlledParticle() const
{
	if (Chaos::FPhysicsObject* PhysicsObject = GetPhysicsObject())
	{
		return FPhysicsObjectExternalInterface::LockRead(PhysicsObject)->GetRigidParticle(PhysicsObject);
	}

	return nullptr;
}

void UChaosMoverBackendComponent::InitSimulation()
{
	UMoverComponent& MoverComp = GetMoverComponent();

	Chaos::FCharacterGroundConstraintHandle* CharacterConstraintHandle = nullptr;
	if (CharacterGroundConstraint)
	{
		if (Chaos::FCharacterGroundConstraintProxy* Proxy = CharacterGroundConstraint->GetProxy<Chaos::FCharacterGroundConstraintProxy>())
		{
			CharacterConstraintHandle = Proxy->GetPhysicsThreadAPI();
		}
	}
	if (!CharacterConstraintHandle)
	{
		return;
	}

	Chaos::FPBDJointConstraintHandle* JointConstraintHandle = nullptr;
	Chaos::FKinematicGeometryParticleHandle* JointEndPointParticle = nullptr;
	if (ActuationConstraintHandle.IsValid())
	{
		if (Chaos::FJointConstraintPhysicsProxy* Proxy = ActuationConstraintHandle->GetProxy<Chaos::FJointConstraintPhysicsProxy>())
		{
			JointConstraintHandle = Proxy->GetHandle();
			if (Chaos::FSingleParticlePhysicsProxy* EndPointProxy = Proxy->GetConstraint()->GetKinematicEndPoint())
			{
				JointEndPointParticle = EndPointProxy->GetHandle_LowLevel()->CastToKinematicParticle();
			}
		}
	}
	if (!JointConstraintHandle || !JointEndPointParticle)
	{
		return;
	}

	UChaosMoverSimulation::FInitParams Params;
	for (const TPair<FName, TObjectPtr<UBaseMovementMode>>& Pair : MoverComp.MovementModes)
	{
		Params.ModesToRegister.Add(Pair.Key, TWeakObjectPtr<UBaseMovementMode>(Pair.Value.Get()));
	}
	for (const TObjectPtr<UBaseMovementModeTransition>& Transition : MoverComp.Transitions)
	{
		Params.TransitionsToRegister.Add(TWeakObjectPtr<UBaseMovementModeTransition>(Transition.Get()));
	}
	Params.MovementMixer = TWeakObjectPtr<UMovementMixer>(MoverComp.MovementMixer.Get());
	Params.ImmediateModeTransition = TWeakObjectPtr<UImmediateMovementModeTransition>(ImmediateModeTransition.Get());
	Params.NullMovementMode = TWeakObjectPtr<UNullMovementMode>(NullMovementMode.Get());
	Params.StartingMovementMode = MoverComp.StartingMovementMode;
	Params.CharacterConstraintHandle = CharacterConstraintHandle;
	Params.ActuationConstraintHandle = JointConstraintHandle;
	Params.ActuationConstraintEndPointParticleHandle = JointEndPointParticle;
	Params.TransformOnInit = TransformOnInit;
	Params.PhysicsObject = GetPhysicsObject();
	Params.Solver = GetPhysicsSolver();
	Params.World = GetWorld();
	
	SimOutputRecord.Clear();

	UE::ChaosMover::FSimulationOutputData OutputData;
	FMoverAuxStateContext UnusedAuxState;
	MoverComp.InitializeSimulationState(&OutputData.SyncState, &UnusedAuxState);

	FMoverTimeStep TimeStep;
	if (Chaos::FPhysicsSolver* Solver = GetPhysicsSolver())
	{
		TimeStep.BaseSimTimeMs = Solver->GetPhysicsResultsTime_External() * 1000.0;
		TimeStep.ServerFrame = Solver->GetCurrentFrame();
		TimeStep.StepMs = Solver->GetAsyncDeltaTime() * 1000.0f;
	}

	SimOutputRecord.Add(TimeStep, OutputData);

	Params.InitialSyncState = OutputData.SyncState;

	Simulation->Init(Params);

	bWantsCreateSim = false;
}

void UChaosMoverBackendComponent::DeinitSimulation()
{
	Simulation->Deinit();
	bWantsDestroySim = false;
}

void UChaosMoverBackendComponent::BeginPlay()
{
	Super::BeginPlay();

	if (UWorld* World = GetWorld(); World && World->IsGameWorld())
	{
		CreatePhysics();

		// Register with the world subsystem
		if (UChaosMoverSubsystem* ChaosMoverSubsystem = UWorld::GetSubsystem<UChaosMoverSubsystem>(GetWorld()))
		{
			ChaosMoverSubsystem->Register(this);
		}

		// Register a callback to watch for component state changes
		if (UPrimitiveComponent* PrimComp = Cast<UPrimitiveComponent>(GetMoverComponent().GetUpdatedComponent()))
		{
			PrimComp->OnComponentPhysicsStateChanged.AddUniqueDynamic(this, &ThisClass::HandleUpdatedComponentPhysicsStateChanged);
		}
	}
}

void UChaosMoverBackendComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	DeinitSimulation();
	DestroyPhysics();

	if (UPrimitiveComponent* PrimComp = Cast<UPrimitiveComponent>(GetMoverComponent().GetUpdatedComponent()))
	{
		PrimComp->OnComponentPhysicsStateChanged.RemoveDynamic(this, &ThisClass::HandleUpdatedComponentPhysicsStateChanged);
	}

	if (UChaosMoverSubsystem* ChaosMoverSubsystem = UWorld::GetSubsystem<UChaosMoverSubsystem>(GetWorld()))
	{
		ChaosMoverSubsystem->Unregister(this);
	}

	Super::EndPlay(EndPlayReason);
}

double UChaosMoverBackendComponent::GetCurrentSimTimeMs()
{
	// Note: this is implicitly an _External function
	if (const Chaos::FPhysicsSolver* Solver = GetPhysicsSolver())
	{
		return Solver->IsUsingAsyncResults() ? Solver->GetAsyncDeltaTime() * GetCurrentSimFrame() * 1000.0 : Solver->GetSolverTime() * 1000.0;
	}
	return 0.0;
}

int32 UChaosMoverBackendComponent::GetCurrentSimFrame()
{
	// Note: this is implicitly an _External function
	if (UWorld* World = GetWorld())
	{
		UE::NetworkPhysicsUtils::GetUpcomingServerFrame_External(World);
	}
	return 0;
}

float UChaosMoverBackendComponent::GetEventSchedulingMinDelaySeconds() const
{
	// The event scheduling falls back to using MaxSupportedLatencyPrediction, but this is usually quite high (e.g. 1 second by default, 0.6 seconds on Fortnite)
	// We expect this function to only be called once on init of the mover component, so we can bear the cost of calling FindComponentByClass here.
	float EventSchedulingMinDelaySeconds = 0.3f;
	if (UNetworkPhysicsSettingsComponent* NetworkPhysicsSettingsComponent = GetOwner()->FindComponentByClass<UNetworkPhysicsSettingsComponent>())
	{
		EventSchedulingMinDelaySeconds = NetworkPhysicsSettingsComponent->GetSettings().GeneralSettings.EventSchedulingMinDelaySeconds;
	}
	else
	{
		EventSchedulingMinDelaySeconds = UPhysicsSettings::Get()->PhysicsPrediction.MaxSupportedLatencyPrediction;
	}

	return EventSchedulingMinDelaySeconds;
}

bool UChaosMoverBackendComponent::IsAsync() const
{
	return true;
}

UChaosMoverSimulation* UChaosMoverBackendComponent::GetSimulation()
{
	return Simulation;
}

const UChaosMoverSimulation* UChaosMoverBackendComponent::GetSimulation() const
{
	return Simulation;
}

void UChaosMoverBackendComponent::ProduceInputData(int32 PhysicsStep, int32 NumSteps, const FMoverTimeStep& TimeStep, UE::ChaosMover::FSimulationInputData& InputData)
{
	Chaos::EnsureIsInGameThreadContext();

	// Recreate the simulation if necessary
	if (bWantsDestroySim)
	{
		DeinitSimulation();
		return;
	}
	if (bWantsCreateSim)
	{
		InitSimulation();
	}

	if (bWantsCreateSim)
	{
		return;
	}

	bool bIsGeneratingInputsLocally = !NetworkPhysicsComponent || NetworkPhysicsComponent->IsLocallyControlled();
	if (bIsGeneratingInputsLocally)
	{
		GenerateInput(TimeStep, InputData);
		// Input generating simulations inject instant movement effects from the Game Thread into the simulation input collection
		// It will be used directly by the state machine (same as when it is received from the network or overwritten during resim),
		// and will also be networked via NetInputCmd
		InjectInstantMovementEffectsIntoInput(TimeStep, InputData);
	}
	else
	{
		// Transfer queued instant movement effects to the simulation
		// Instant effects should still be consumed by a non input generating actor on a server if it is not controlled remotely (this is an approximation of "no other instance is input producing")
		InjectInstantMovementEffectsIntoSim(TimeStep);
	}

	// Add default simulation input data
	FChaosMoverSimulationDefaultInputs& SimInputs = Simulation->GetLocalSimInput_Mutable().FindOrAddMutableDataByType<FChaosMoverSimulationDefaultInputs>();
	UMoverComponent& MoverComp = GetMoverComponent();

	SimInputs.Gravity = MoverComp.GetGravityAcceleration();
	SimInputs.UpDir = MoverComp.GetUpDirection();
	SimInputs.bIsGeneratingInputsLocally = bIsGeneratingInputsLocally;
	APawn* OwnerAsPawn = Cast<APawn>(GetOwner());
	SimInputs.bIsRemotelyControlled = OwnerAsPawn ? (OwnerAsPawn->GetController() && !OwnerAsPawn->IsLocallyControlled()) : false;
	SimInputs.OwningActor = GetOwner();
	SimInputs.World = GetWorld();

	if (UPrimitiveComponent* PrimComp = Cast<UPrimitiveComponent>(MoverComp.GetUpdatedComponent()))
	{
		SimInputs.CollisionQueryParams = FCollisionQueryParams(SCENE_QUERY_STAT(ChaosMoverQuery), false, PrimComp->GetOwner());
		SimInputs.CollisionQueryParams.bTraceIntoSubComponents = false;
		SimInputs.CollisionResponseParams = FCollisionResponseParams(ECR_Overlap);
		SimInputs.CollisionResponseParams.CollisionResponse.SetResponse(ECC_WorldStatic, ECR_Block);
		SimInputs.CollisionResponseParams.CollisionResponse.SetResponse(ECC_WorldDynamic, ECR_Block);
		SimInputs.CollisionResponseParams.CollisionResponse.SetResponse(ECC_Vehicle, ECR_Block);
		SimInputs.CollisionResponseParams.CollisionResponse.SetResponse(ECC_Destructible, ECR_Block);
		SimInputs.CollisionResponseParams.CollisionResponse.SetResponse(ECC_PhysicsBody, ECR_Block);
		PrimComp->InitSweepCollisionParams(SimInputs.CollisionQueryParams, SimInputs.CollisionResponseParams);

		SimInputs.CollisionChannel = PrimComp->GetCollisionObjectType();
		PrimComp->CalcBoundingCylinder(SimInputs.PawnCollisionRadius, SimInputs.PawnCollisionHalfHeight);
	}
	if (IPhysicsComponent* PhysComp = Cast<IPhysicsComponent>(MoverComp.GetUpdatedComponent()))
	{
		SimInputs.PhysicsObject = PhysComp->GetPhysicsObjectById(0); // Get the root physics object
	}
	if (const APhysicsVolume* CurPhysVolume = MoverComp.GetUpdatedComponent()->GetPhysicsVolume())
	{
		SimInputs.PhysicsObjectGravity = CurPhysVolume->GetGravityZ();
	}

	if (MoverComp.OnPreSimulationTick.IsBound())
	{
		MoverComp.OnPreSimulationTick.Broadcast(TimeStep, InputData.InputCmd);
	}
}

void UChaosMoverBackendComponent::GenerateInput(const FMoverTimeStep& TimeStep, UE::ChaosMover::FSimulationInputData& InputData)
{
	UMoverComponent& MoverComp = GetMoverComponent();
	MoverComp.ProduceInput(TimeStep.StepMs, &InputData.InputCmd);
}

void UChaosMoverBackendComponent::InjectInstantMovementEffectsIntoInput(const FMoverTimeStep& TimeStep, UE::ChaosMover::FSimulationInputData& InputData)
{
	UMoverComponent& MoverComp = GetMoverComponent();
	// Add all queued instant effects to the input so it can be networked
	const TArray<FScheduledInstantMovementEffect>& ScheduledInstantMovementEffect = MoverComp.GetQueuedInstantMovementEffects();
	FChaosNetInstantMovementEffectsQueue& InstantMovementEffectsQueue = InputData.InputCmd.InputCollection.FindOrAddMutableDataByType<FChaosNetInstantMovementEffectsQueue>();
	InstantMovementEffectsQueue.Effects.Empty();
	if (!ScheduledInstantMovementEffect.IsEmpty())
	{
		for (const FScheduledInstantMovementEffect& ScheduledEffect : MoverComp.GetQueuedInstantMovementEffects())
		{
			uint8 UniqueID = NextInstantMovementEffectUniqueID++;
			if (UniqueID == 0xFF)
			{
				UniqueID = 0;
				NextInstantMovementEffectUniqueID = 1;
			}

			InstantMovementEffectsQueue.Add(ScheduledEffect, TimeStep.ServerFrame, UniqueID);

#if !defined(BUILD_SHIPPING) || !BUILD_SHIPPING
			ENetMode NetMode = GetWorld() ? GetWorld()->GetNetMode() : NM_MAX;
			UE_LOG(LogChaosMover, Verbose, TEXT("(%s) UChaosMoverBackendComponent::InjectInstantMovementEffects: Transferring Instant Effect Scheduled for frame %d at frame %d (Assigning Net ID %d) from the mover component to the async simulation: %s."),
				*ToString(NetMode), ScheduledEffect.ExecutionServerFrame, TimeStep.ServerFrame, UniqueID, ScheduledEffect.Effect.IsValid() ? *ScheduledEffect.Effect->ToSimpleString() : TEXT("INVALID INSTANT EFFECT"));
#endif // !defined(BUILD_SHIPPING) || !BUILD_SHIPPING
		}
		MoverComp.ClearQueuedInstantMovementEffects();
	}
}

void UChaosMoverBackendComponent::InjectInstantMovementEffectsIntoSim(const FMoverTimeStep& TimeStep)
{
	UMoverComponent& MoverComp = GetMoverComponent();
	FChaosMoverSimulationDefaultInputs& SimInputs = Simulation->GetLocalSimInput_Mutable().FindOrAddMutableDataByType<FChaosMoverSimulationDefaultInputs>();
	ENetMode NetMode = SimInputs.OwningActor ? SimInputs.OwningActor->GetNetMode() : NM_MAX;
	if (NetMode == NM_DedicatedServer || (NetMode == NM_ListenServer && !SimInputs.bIsRemotelyControlled))
	{
		for (const FScheduledInstantMovementEffect& ScheduledEffect : MoverComp.GetQueuedInstantMovementEffects())
		{
			Simulation->QueueInstantMovementEffect(ScheduledEffect, /*bShouldRollBack =*/ false); // Resims do not rerun gameplay logic so we should not roll back effects issued by the game thread

			UE_LOG(LogChaosMover, Verbose, TEXT("(%s) ProduceInputData: Async simulation received an Instant Movement Effect from the Mover Component, scheduled for frame %d at frame %d: %s"),
				*ToString(NetMode), ScheduledEffect.ExecutionServerFrame, TimeStep.ServerFrame, ScheduledEffect.Effect.IsValid() ? *ScheduledEffect.Effect->ToSimpleString() : TEXT("INVALID INSTANT EFFECT"));
		}
	}
}

void UChaosMoverBackendComponent::ConsumeOutputData(const FMoverTimeStep& TimeStep, const UE::ChaosMover::FSimulationOutputData& OutputData)
{
	Chaos::EnsureIsInGameThreadContext();

	if (bWantsCreateSim)
	{
		return;
	}

	SimOutputRecord.Add(TimeStep, OutputData);
}

void UChaosMoverBackendComponent::FinalizeFrame(double ResultsTimeInMs)
{
	Chaos::EnsureIsInGameThreadContext();

	if (bWantsCreateSim)
	{
		return;
	}

	UMoverComponent& MoverComp = GetMoverComponent();

	FMoverTimeStep TimeStep;
	UE::ChaosMover::FSimulationOutputData InterpolatedOutput;
	SimOutputRecord.CreateInterpolatedResult(ResultsTimeInMs, TimeStep, InterpolatedOutput);

	// Physics interactions in the last frame may have caused a change in position or velocity that's different from what a simple lerp would predict,
	// so stomp the lerped sync state's transform data with that of the actual particle after the last sim frame
	FMoverDefaultSyncState& TransformSyncState = InterpolatedOutput.SyncState.SyncStateCollection.FindOrAddMutableDataByType<FMoverDefaultSyncState>();
	if (Chaos::FPBDRigidParticle* Particle = GetControlledParticle())
	{
		TransformSyncState.SetTransforms_WorldSpace(Particle->GetX(),
			FRotator(Particle->GetR()),
			Particle->GetV(),
			FMath::RadiansToDegrees(Particle->GetW()),
			TransformSyncState.GetMovementBase(),
			TransformSyncState.GetMovementBaseBoneName());

		// Make sure the move direction intent is in base space (the base quat is identity if there's no base, effectively making this a no-op)
		TransformSyncState.MoveDirectionIntent = TransformSyncState.GetCapturedMovementBaseQuat().UnrotateVector(TransformSyncState.MoveDirectionIntent);
	
		check(Chaos::FVec3::IsNearlyEqual(Chaos::FVec3(TransformSyncState.GetLocation_WorldSpace()), Particle->GetX(), UE_KINDA_SMALL_NUMBER));
	}

	MoverComp.SetSimulationOutput(TimeStep, InterpolatedOutput);
	
	if (MoverComp.OnPostSimulationTick.IsBound())
	{
		MoverComp.OnPostSimulationTick.Broadcast(TimeStep);
	}

	if (MoverComp.OnPostFinalize.IsBound())
	{
		FMoverAuxStateContext UnusedAuxStateContext;
		MoverComp.OnPostFinalize.Broadcast(InterpolatedOutput.SyncState, UnusedAuxStateContext);
	}
}

// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosMover/PathedMovement/ChaosPathedMovementControllerComponent.h"
#include "ChaosMover/PathedMovement/ChaosPathedMovementDebugDrawComponent.h"
#include "ChaosMover/PathedMovement/ChaosPathedMovementMode.h"
#include "ChaosMover/ChaosMoverLog.h"
#include "PBDRigidsSolver.h"
#include "Physics/Experimental/PhysScene_Chaos.h"
#include "MoverComponent.h"
#include "TimerManager.h"
#include "Engine/World.h"
#include "Physics/NetworkPhysicsComponent.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(ChaosPathedMovementControllerComponent)

UChaosPathedMovementControllerComponent::UChaosPathedMovementControllerComponent()
{
#if ENABLE_DRAW_DEBUG
	DebugDrawComp = CreateDefaultSubobject<UChaosPathedMovementDebugDrawComponent>(TEXT("ChaosPathedMovementDebugDraw"));

	// Since the debug draw comp relies on data in this component, and this component will often need to be re-registered following the construction script,
	// we want registration of the debug draw comp to match (and follow) the registration state of this component
	DebugDrawComp->bAutoRegister = false;
#endif
}

void UChaosPathedMovementControllerComponent::OnRegister()
{
	Super::OnRegister();

	AActor* Owner = GetOwner();
	if (UMoverComponent* OwnerMoverComponent = Owner ? Owner->FindComponentByClass<UMoverComponent>() : nullptr)
	{
		for (TPair<FName, TObjectPtr<UBaseMovementMode>>& NameModePair : OwnerMoverComponent->MovementModes)
		{
			if (UChaosPathedMovementMode* PathedMovementMode = Cast<UChaosPathedMovementMode>(NameModePair.Value))
			{
				PathedMovementMode->InitializePath();
			}
		}	
	}

#if ENABLE_DRAW_DEBUG
	DebugDrawComp->RegisterComponent();
#endif
}

void UChaosPathedMovementControllerComponent::OnUnregister()
{
	Super::OnUnregister();

#if ENABLE_DRAW_DEBUG
	DebugDrawComp->UnregisterComponent();
#endif
}

void UChaosPathedMovementControllerComponent::BeginPlay()
{
	Super::BeginPlay();

	// Subscribe to OnPostMovement to process simulation output
	if (AActor* Owner = GetOwner())
	{
		if (UMoverComponent* OwnerMoverComponent = Owner->FindComponentByClass<UMoverComponent>())
		{
			MoverComponent = OwnerMoverComponent;
			MoverComponent->OnPostFinalize.AddDynamic(this, &UChaosPathedMovementControllerComponent::OnPostFinalize);
			MoverComponent->OnPostSimEventReceived.AddUObject(this, &UChaosPathedMovementControllerComponent::OnPostSimEventReceived);
		}
	}

	// The event scheduling falls back to using MaxSupportedLatencyPrediction, but this is usually quite high (e.g. 1 second by default, 0.6 seconds on Fortnite)
	// We may want to have a different default value if this is too high when a network physics settings component is not present
	EventSchedulingMinDelaySeconds = UPhysicsSettings::Get()->PhysicsPrediction.MaxSupportedLatencyPrediction;
	if (UNetworkPhysicsSettingsComponent* NetworkPhysicsSettingsComponent = GetOwner()->FindComponentByClass<UNetworkPhysicsSettingsComponent>())
	{
		EventSchedulingMinDelaySeconds = NetworkPhysicsSettingsComponent->GetSettings().GeneralSettings.EventSchedulingMinDelaySeconds;
	}

	const UWorld* World = GetWorld();
	if (GetOwnerRole() == ROLE_Authority || (World && World->IsNetMode(ENetMode::NM_Standalone)))
	{
		ExecuteOrScheduleChange(InitialProperties, /*bIsScheduled =*/ true);
	}
}

void UChaosPathedMovementControllerComponent::ProduceInput_Implementation(int32 SimTimeMs, FMoverInputCmdContext& InputCmdResult)
{
	FChaosPathedMovementInputs& PathedMovementInputs = InputCmdResult.InputCollection.FindOrAddMutableDataByType<FChaosPathedMovementInputs>();
	PathedMovementInputs.LastChangeFrame = LastChangeFrame;
	PathedMovementInputs.MovementStartFrame = MovementStartFrame;
	PathedMovementInputs.Props = PathedMovementMutableProperties;
}

void UChaosPathedMovementControllerComponent::OnPostFinalize(const FMoverSyncState& SyncState, const FMoverAuxStateContext& AuxState)
{
	if (const FChaosPathedMovementState* FoundPostSimState = SyncState.SyncStateCollection.FindDataByType<FChaosPathedMovementState>())
	{
		PostSimState = *FoundPostSimState;
	}
}

void UChaosPathedMovementControllerComponent::OnPostSimEventReceived(const FMoverSimulationEventData& EventData)
{
	if (const FPathedMovementStartedEventData* PathedMovementStartedEventData = EventData.CastTo<FPathedMovementStartedEventData>())
	{
		OnPathedMovementStarted.Broadcast();
	}
	else if (const FPathedMovementStoppedEventData* PathedMovementStoppedEventData = EventData.CastTo<FPathedMovementStoppedEventData>())
	{
		OnPathedMovementStopped.Broadcast(PathedMovementStoppedEventData->bReachedEndOfPlayback);
	}
	else if (const FPathedMovementBouncedEventData* PathedMovementBouncedEventData = EventData.CastTo<FPathedMovementBouncedEventData>())
	{
		OnPathedMovementBounced.Broadcast();
	}
	else if (const FPathedMovementInReverseChangedEventData* PathedMovementInReverseChangedEventData = EventData.CastTo<FPathedMovementInReverseChangedEventData>())
	{
		OnPathedMovementReversePlaybackChanged.Broadcast(PathedMovementInReverseChangedEventData->bNewInReverse);
	}
	else if (const FPathedMovementLoopingChangedEventData* PathedMovementLoopingChangedEventData = EventData.CastTo<FPathedMovementLoopingChangedEventData>())
	{
		OnPathedMovementLoopingPlaybackChanged.Broadcast(PathedMovementLoopingChangedEventData->bNewLooping);
	}
	else if (const FPathedMovementOneWayChangedEventData* PathedMovementOneWayChangedEventData = EventData.CastTo<FPathedMovementOneWayChangedEventData>())
	{
		OnPathedMovementOneWayPlaybackChanged.Broadcast(PathedMovementOneWayChangedEventData->bNewOneWay);
	}
}

bool ShouldExecute(EChaosPathedMovementExecutionType ExecutionType, AActor* OwnerActor, ENetMode NetMode, const TCHAR* FunctionNameForDebugging)
{
	bool bShouldExecute = false;

	if (ensure(OwnerActor))
	{
		// Always execute in standalone
		if (NetMode == NM_Standalone)
		{
			bShouldExecute = true;
		}
		else
		{
			ENetRole OwnerNetRole = OwnerActor->GetLocalRole();
			if (ExecutionType == EChaosPathedMovementExecutionType::AuthorityOnly)
			{
				// Only execute if we're authority on the actor
				bShouldExecute = (OwnerNetRole == ROLE_Authority);
			}
			else
			{
				// Don't execute on the server, unless it is locally controlled
				if (NetMode == NM_DedicatedServer)
				{
					bShouldExecute = false;
				}
				else if (NetMode == NM_ListenServer)
				{
					// Predict on the listen server if it is the locally controlled pawn or controller
					ENetRole RemoteRole = OwnerActor->GetRemoteRole();
					bShouldExecute = (RemoteRole == ROLE_AutonomousProxy);
				}
				else
				{
					bShouldExecute = (OwnerNetRole != ROLE_SimulatedProxy);
				}
			}
		}

		if (!bShouldExecute)
		{
			UE_LOG(LogChaosMover, Log, TEXT("%s will be ignored, called with ExecutionType = %s, OwnerLocalRole = %s, OwnerRemoteRole = %s, NetMode = %s."),
				FunctionNameForDebugging,
				*StaticEnum<EChaosPathedMovementExecutionType>()->GetNameStringByValue((int64)ExecutionType),
				*StaticEnum<ENetRole>()->GetNameStringByValue((int64)OwnerActor->GetLocalRole()),
				*StaticEnum<ENetRole>()->GetNameStringByValue((int64)OwnerActor->GetRemoteRole()),
				*ToString(NetMode));
		}
	}
	return bShouldExecute;
}

void UChaosPathedMovementControllerComponent::RequestStartPlayingPath(EChaosPathedMovementExecutionType ExecutionType, bool bIsScheduled)
{
	if (!ShouldExecute(ExecutionType, GetOwner(), GetNetMode(), TEXT("UChaosPathedMovementControllerComponent::RequestStartPlayingPath")))
	{
		return;
	}

	Chaos::FPhysicsSolver* Solver = GetPhysicsSolver();
	const UWorld* World = GetWorld();
	if (!ensure(Solver) || !World)
	{
		return;
	}
	
	if (!PostSimState.PropertiesInEffect.bWantsToPlay || PostSimState.bHasFinished)
	{
		FChaosMutablePathedMovementProperties RequestedPathedMovementMutableProperties = PathedMovementMutableProperties;
		RequestedPathedMovementMutableProperties.bWantsToPlay = true;
		ExecuteOrScheduleChange(RequestedPathedMovementMutableProperties, bIsScheduled, /*bIsStart =*/ true);
	}
}

void UChaosPathedMovementControllerComponent::RequestStopPlayingPath(EChaosPathedMovementExecutionType ExecutionType, bool bIsScheduled)
{
	if (!ShouldExecute(ExecutionType, GetOwner(), GetNetMode(), TEXT("UChaosPathedMovementControllerComponent::RequestStopPlayingPath")))
	{
		return;
	}

	FChaosMutablePathedMovementProperties RequestedPathedMovementMutableProperties = PathedMovementMutableProperties;
	RequestedPathedMovementMutableProperties.bWantsToPlay = false;
	ExecuteOrScheduleChange(RequestedPathedMovementMutableProperties, bIsScheduled);
}

bool UChaosPathedMovementControllerComponent::WantsPlayingPath() const
{
	return PathedMovementMutableProperties.bWantsToPlay;
}

bool UChaosPathedMovementControllerComponent::IsPlayingPath() const
{
	return PostSimState.PropertiesInEffect.bWantsToPlay && !PostSimState.bHasFinished;
}

void UChaosPathedMovementControllerComponent::RequestReversePlayback(bool bWantsReversePlayback, EChaosPathedMovementExecutionType ExecutionType, bool bIsScheduled)
{
	if (!ShouldExecute(ExecutionType, GetOwner(), GetNetMode(), TEXT("UChaosPathedMovementControllerComponent::RequestReversePlayback")))
	{
		return;
	}

	FChaosMutablePathedMovementProperties RequestedPathedMovementMutableProperties = PathedMovementMutableProperties;
	RequestedPathedMovementMutableProperties.bWantsReversePlayback = bWantsReversePlayback;
	ExecuteOrScheduleChange(RequestedPathedMovementMutableProperties, bIsScheduled);
}

bool UChaosPathedMovementControllerComponent::WantsReversePlayback() const
{
	return PathedMovementMutableProperties.bWantsReversePlayback;
}

bool UChaosPathedMovementControllerComponent::IsReversePlayback() const
{
	return PostSimState.PropertiesInEffect.bWantsReversePlayback;
}

void UChaosPathedMovementControllerComponent::RequestLoopingPlayback(bool bWantsLoopingPlayback, EChaosPathedMovementExecutionType ExecutionType, bool bIsScheduled)
{
	if (!ShouldExecute(ExecutionType, GetOwner(), GetNetMode(), TEXT("UChaosPathedMovementControllerComponent::RequestLoopingPlayback")))
	{
		return;
	}

	FChaosMutablePathedMovementProperties RequestedPathedMovementMutableProperties = PathedMovementMutableProperties;
	RequestedPathedMovementMutableProperties.bWantsLoopingPlayback = bWantsLoopingPlayback;
	ExecuteOrScheduleChange(RequestedPathedMovementMutableProperties, bIsScheduled);
}

bool UChaosPathedMovementControllerComponent::WantsLoopingPlayback() const
{
	return PathedMovementMutableProperties.bWantsLoopingPlayback;
}

bool UChaosPathedMovementControllerComponent::IsLoopingPlayback() const
{
	return PostSimState.PropertiesInEffect.bWantsLoopingPlayback;
}

void UChaosPathedMovementControllerComponent::RequestOneWayPlayback(bool bWantsOneWayPlayback, EChaosPathedMovementExecutionType ExecutionType, bool bIsScheduled)
{
	if (!ShouldExecute(ExecutionType, GetOwner(), GetNetMode(), TEXT("UChaosPathedMovementControllerComponent::RequestOneWayPlayback")))
	{
		return;
	}

	FChaosMutablePathedMovementProperties RequestedPathedMovementMutableProperties = PathedMovementMutableProperties;
	RequestedPathedMovementMutableProperties.bWantsOneWayPlayback = bWantsOneWayPlayback;
	ExecuteOrScheduleChange(RequestedPathedMovementMutableProperties, bIsScheduled);
}

bool UChaosPathedMovementControllerComponent::WantsOneWayPlayback() const
{
	return PathedMovementMutableProperties.bWantsOneWayPlayback;
}

bool UChaosPathedMovementControllerComponent::IsOneWayPlayback() const
{
	return PostSimState.PropertiesInEffect.bWantsOneWayPlayback;
}

Chaos::FPhysicsSolver* UChaosPathedMovementControllerComponent::GetPhysicsSolver() const
{
	UWorld* World = GetWorld();
	FPhysScene* Scene = World ? World->GetPhysicsScene() : nullptr;
	return Scene ? Scene->GetSolver() : nullptr;
}

void UChaosPathedMovementControllerComponent::ExecuteOrScheduleChange(const FChaosMutablePathedMovementProperties& NewProperties, bool bIsScheduled, bool bIsStart)
{
	Chaos::FPhysicsSolver* Solver = GetPhysicsSolver();
	if (!Solver)
	{
		return;
	}

	auto LambdaChangeMutableProperties = [&](const FChaosMutablePathedMovementProperties& InProperties, int32 ChangeFrame, bool bIsStart)
	{
		LastChangeFrame = ChangeFrame;
		FChaosMutablePathedMovementProperties PreviousProperties = PathedMovementMutableProperties;
		PathedMovementMutableProperties = InProperties;
		if (NewProperties.bWantsToPlay && bIsStart)
		{
			// Note that we do this even if we were already playing
			// This is to be able to restart a non looping path movement which had reached the end
			// without having to first stop it then start it again
			MovementStartFrame = ChangeFrame;
		}
	};

	if (Solver->IsUsingAsyncResults())
	{
		int32 ExecutionServerFrame = UE::NetworkPhysicsUtils::GetUpcomingServerFrame_External(GetWorld());
		if (bIsScheduled)
		{
			const double AsyncDeltaTime = Solver->GetAsyncDeltaTime();
			const int32 DelayFrames = (AsyncDeltaTime > UE_SMALL_NUMBER) ? FMath::CeilToInt32(EventSchedulingMinDelaySeconds / AsyncDeltaTime) : 0;
			ExecutionServerFrame += DelayFrames;
		}
		LambdaChangeMutableProperties(NewProperties, ExecutionServerFrame, bIsStart);
	}
	else if (const UWorld* World = GetWorld())
	{
		// In a standalone game that isn't using async physics, the start delay can't be reliably converted to a physics frame.
		// So we have to use a world timer instead of delaying the start frame

		FTimerManager& TimerManager = World->GetTimerManager();
		if (PathedMovementDelayedStartTimerHandle.IsValid())
		{
			TimerManager.ClearTimer(PathedMovementDelayedStartTimerHandle);
		}

		TimerManager.SetTimer(PathedMovementDelayedStartTimerHandle, FTimerDelegate::CreateWeakLambda(this, [this, NewProperties, bIsStart, LambdaChangeMutableProperties]()
		{
			if (Chaos::FPhysicsSolver* Solver = GetPhysicsSolver())
			{
				LambdaChangeMutableProperties(NewProperties, Solver->GetCurrentFrame(), bIsStart);
			}
		}), EventSchedulingMinDelaySeconds, false);
	}
}

bool UChaosPathedMovementControllerComponent::ShouldDisplayProgressPreviewMesh_Implementation() const
{
	return bDisplayProgressPreviewMesh;
}

float UChaosPathedMovementControllerComponent::GetPreviewMeshOverallPathProgress_Implementation() const
{
	return PreviewMeshOverallPathProgress;
}

UMaterialInterface* UChaosPathedMovementControllerComponent::GetProgressPreviewMeshMaterial_Implementation() const
{
	return PreviewMeshMaterial;
}

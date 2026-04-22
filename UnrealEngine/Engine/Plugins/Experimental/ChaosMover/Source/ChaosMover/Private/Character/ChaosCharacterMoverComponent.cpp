// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosMover/Character/ChaosCharacterMoverComponent.h"

#include "ChaosMover/Backends/ChaosMoverBackend.h"
#include "ChaosMover/Character/ChaosCharacterInputs.h"
#include "ChaosMover/Character/Modes/ChaosFallingMode.h"
#include "ChaosMover/Character/Modes/ChaosFlyingMode.h"
#include "ChaosMover/Character/Modes/ChaosWalkingMode.h"
#include "ChaosMover/ChaosMoverLog.h"
#include "DefaultMovementSet/CharacterMoverSimulationTypes.h"
#include "GameFramework/Pawn.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(ChaosCharacterMoverComponent)

UChaosCharacterMoverComponent::UChaosCharacterMoverComponent()
{
	// Default movement modes
	MovementModes.Add(DefaultModeNames::Walking, CreateDefaultSubobject<UChaosWalkingMode>(TEXT("DefaultChaosWalkingMode")));
	MovementModes.Add(DefaultModeNames::Falling, CreateDefaultSubobject<UChaosFallingMode>(TEXT("DefaultChaosFallingMode")));
	MovementModes.Add(DefaultModeNames::Flying, CreateDefaultSubobject<UChaosFlyingMode>(TEXT("DefaultChaosFlyingMode")));

	StartingMovementMode = DefaultModeNames::Falling;

	bHandleJump = false;
	bHandleStanceChanges = false;

	BackendClass = UChaosMoverBackendComponent::StaticClass();
}

void UChaosCharacterMoverComponent::OnMoverPreSimulationTick(const FMoverTimeStep& TimeStep, const FMoverInputCmdContext& InputCmd)
{
	// This overrides the base class to make sure that it doesn't do anything. We do this
	// because the stance modifier and jump handling of the base is not async physics friendly
}

void UChaosCharacterMoverComponent::ProcessSimulationEvent(const FMoverSimulationEventData& EventData)
{
	Super::ProcessSimulationEvent(EventData);

	if (const FLandedEventData* LandedData = EventData.CastTo<FLandedEventData>())
	{
		OnLanded.Broadcast(LandedData->NewModeName, LandedData->HitResult);
	}
	if (const FJumpedEventData* JumpedData = EventData.CastTo<FJumpedEventData>())
	{
		OnJumped.Broadcast(JumpedData->JumpStartHeight);
	}
	if (const FStanceModifiedEventData* StanceModifiedEvent = EventData.CastTo<FStanceModifiedEventData>())
	{
		OnStanceModified(*StanceModifiedEvent);
	}
}

void UChaosCharacterMoverComponent::DoQueueNextMode(FName DesiredModeName, bool bShouldReenter)
{
	Super::DoQueueNextMode(DesiredModeName, bShouldReenter);

	if (QueuedModeTransitionName != NAME_None)
	{ 
		const FName NextModeName = QueuedModeTransitionName;
		if (NextModeName != NAME_None)
		{
			UE_LOG(LogChaosMover, Log, TEXT("%s (%s) Overwriting of queued mode change (%s) with (%s)"), *GetNameSafe(GetOwner()), *UEnum::GetValueAsString(GetOwner()->GetLocalRole()), *NextModeName.ToString(), *DesiredModeName.ToString());
		}
	}

	QueuedModeTransitionName = DesiredModeName;
}

void UChaosCharacterMoverComponent::ClearQueuedMode()
{
	QueuedModeTransitionName = NAME_None;
}

void UChaosCharacterMoverComponent::SetAdditionalSimulationOutput(const FMoverDataCollection& Data)
{
	Super::SetAdditionalSimulationOutput(Data);

	if (const FFloorResultData* FloorData = Data.FindDataByType<FFloorResultData>())
	{
		bFloorResultSet = true;
		LatestFloorResult = FloorData->FloorResult;
	}
}

bool UChaosCharacterMoverComponent::TryGetFloorCheckHitResult(FHitResult& OutHitResult) const
{
	if (bFloorResultSet)
	{
		OutHitResult = LatestFloorResult.HitResult;
		return true;
	}
	else
	{
		return Super::TryGetFloorCheckHitResult(OutHitResult);
	}
}

void UChaosCharacterMoverComponent::ProduceInput(const int32 DeltaTimeMS, FMoverInputCmdContext* Cmd)
{
	Super::ProduceInput(DeltaTimeMS, Cmd);

	check(Cmd);

	// Add launch input data if launch velocity is set
	if (!LaunchVelocityOrImpulse.IsZero())
	{
		FChaosMoverLaunchInputs& LaunchInputs = Cmd->InputCollection.FindOrAddMutableDataByType<FChaosMoverLaunchInputs>();
		LaunchInputs.LaunchVelocityOrImpulse = LaunchVelocityOrImpulse;
		LaunchInputs.Mode = LaunchMode;

		LaunchVelocityOrImpulse = FVector::ZeroVector;
	}

	if (QueuedModeTransitionName != NAME_None)
	{
		FCharacterDefaultInputs& CharacterDefaultInputs = Cmd->InputCollection.FindOrAddMutableDataByType<FCharacterDefaultInputs>();
		CharacterDefaultInputs.SuggestedMovementMode = QueuedModeTransitionName;

		ClearQueuedMode();
	}

	// TODO @Harsha Making bWantsToCrouch sticky because the server is missing the one time crouch input.
	// Investigate the input drops and after the fix, revert this change back to making it non-sticky.
	bWantsToCrouch &= CanCrouch();
	FChaosMoverCrouchInputs& CrouchInputs = Cmd->InputCollection.FindOrAddMutableDataByType<FChaosMoverCrouchInputs>();
	CrouchInputs.bWantsToCrouch = bWantsToCrouch;

	if (bCancelMovementOverrides)
	{
		FChaosMovementSettingsOverridesRemover& MovementSettingsRemover = Cmd->InputCollection.FindOrAddMutableDataByType<FChaosMovementSettingsOverridesRemover>();
		MovementSettingsRemover.ModeName = ModeToOverrideSettings;

		bCancelMovementOverrides = false;
	}

	if (bOverrideMovementSettings)
	{
		FChaosMovementSettingsOverrides& MovementSettings = Cmd->InputCollection.FindOrAddMutableDataByType<FChaosMovementSettingsOverrides>();
		MovementSettings.MaxSpeedOverride = MaxSpeedOverride;
		MovementSettings.AccelerationOverride = AccelerationOverride;
		MovementSettings.ModeName = ModeToOverrideSettings;

		bOverrideMovementSettings = false;
	}
}

void UChaosCharacterMoverComponent::Launch(const FVector& VelocityOrImpulse, EChaosMoverVelocityEffectMode Mode)
{
	LaunchVelocityOrImpulse = VelocityOrImpulse;
	LaunchMode = Mode;
}

void UChaosCharacterMoverComponent::Crouch()
{
	Super::Crouch();
	bCrouchInputPending = true;
}

void UChaosCharacterMoverComponent::UnCrouch()
{
	Super::UnCrouch();
	bCrouchInputPending = true;
}

void UChaosCharacterMoverComponent::OnStanceModified(const FStanceModifiedEventData& EventData)
{
	OnStanceChanged.Broadcast(EventData.OldStance, EventData.NewStance);
}

void UChaosCharacterMoverComponent::OverrideMovementSettings(const FChaosMovementSettingsOverrides Overrides)
{
	ModeToOverrideSettings = Overrides.ModeName;
	MaxSpeedOverride = FMath::Max(Overrides.MaxSpeedOverride, 0.0f);
	AccelerationOverride = FMath::Max(Overrides.AccelerationOverride, 0.0f);
	bOverrideMovementSettings = true;
}

void UChaosCharacterMoverComponent::CancelMovementSettingsOverrides(FName ModeName)
{
	ModeToOverrideSettings = ModeName;
	bCancelMovementOverrides = true;
}

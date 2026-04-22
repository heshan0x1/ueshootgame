// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosMover/Character/Modes/ChaosCharacterMovementMode.h"

#include "Chaos/Character/CharacterGroundConstraintSettings.h"
#include "Chaos/ContactModification.h"
#include "Chaos/PhysicsObjectInternalInterface.h"
#include "ChaosMover/Character/Settings/SharedChaosCharacterMovementSettings.h"
#include "ChaosMover/ChaosMoverLog.h"
#include "ChaosMover/ChaosMoverSimulation.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"
#include "Math/UnitConversion.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(ChaosCharacterMovementMode)

UChaosCharacterMovementMode::UChaosCharacterMovementMode(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SharedSettingsClasses.Add(USharedChaosCharacterMovementSettings::StaticClass());
}

void UChaosCharacterMovementMode::OnRegistered(const FName ModeName)
{
	Super::OnRegistered(ModeName);

	if (TargetHeightOverride.IsSet())
	{
		TargetHeight = TargetHeightOverride.GetValue();
	}
	else if (const ACharacter* CharacterOwner = GetTypedOuter<ACharacter>())
	{
		TargetHeight = -CharacterOwner->GetMesh()->GetRelativeLocation().Z;
	}

	SharedSettings = GetMoverComponent()->FindSharedSettings<USharedChaosCharacterMovementSettings>();
	ensureMsgf(SharedSettings, TEXT("Failed to find instance of USharedChaosCharacterMovementSettings on %s. Movement may not function properly."), *GetPathNameSafe(this));
}

void UChaosCharacterMovementMode::OnUnregistered()
{
	SharedSettings = nullptr;

	Super::OnUnregistered();
}

void UChaosCharacterMovementMode::SetTargetHeightOverride(float InTargetHeight)
{
	TargetHeightOverride = InTargetHeight;
	TargetHeight = InTargetHeight;
}

void UChaosCharacterMovementMode::ClearTargetHeightOverride()
{
	TargetHeightOverride.Reset();

	if (const ACharacter* CharacterOwner = GetTypedOuter<ACharacter>())
	{
		TargetHeight = -CharacterOwner->GetMesh()->GetRelativeLocation().Z;
	}
	else
	{
		TargetHeight = GetDefault<UChaosCharacterMovementMode>(GetClass())->TargetHeight;
	}
}

void UChaosCharacterMovementMode::SetQueryRadiusOverride(float InQueryRadius)
{
	QueryRadiusOverride = InQueryRadius;
	QueryRadius = InQueryRadius;
}

void UChaosCharacterMovementMode::ClearQueryRadiusOverride()
{
	QueryRadiusOverride.Reset();

	if (const ACharacter* CharacterOwner = GetTypedOuter<ACharacter>())
	{
		if (UCapsuleComponent* CapsuleComp = CharacterOwner->GetCapsuleComponent())
		{
			QueryRadius = FMath::Max(CapsuleComp->GetScaledCapsuleRadius() - 5.0f, 0.0f);
			return;
		}
	}
	
	QueryRadius = GetDefault<UChaosCharacterMovementMode>(GetClass())->QueryRadius;
}

void UChaosCharacterMovementMode::UpdateConstraintSettings(Chaos::FCharacterGroundConstraintSettings& ConstraintSettings) const
{
	ConstraintSettings.RadialForceLimit = FUnitConversion::Convert(RadialForceLimit, EUnit::Newtons, EUnit::KilogramCentimetersPerSecondSquared);
	ConstraintSettings.TwistTorqueLimit = FUnitConversion::Convert(TwistTorqueLimit, EUnit::NewtonMeters, EUnit::KilogramCentimetersSquaredPerSecondSquared);
	ConstraintSettings.SwingTorqueLimit = FUnitConversion::Convert(SwingTorqueLimit, EUnit::NewtonMeters, EUnit::KilogramCentimetersSquaredPerSecondSquared);
	ConstraintSettings.TargetHeight = TargetHeight;
}

float UChaosCharacterMovementMode::GetMaxWalkSlopeCosine() const
{
	if (const USharedChaosCharacterMovementSettings* SharedSettingsPtr = SharedSettings.Get())
	{
		return SharedSettingsPtr->GetMaxWalkableSlopeCosine();
	}

	return 0.707f;
}

float UChaosCharacterMovementMode::GetMaxSpeed() const
{
	if (MaxSpeedOverride.IsSet())
	{
		return MaxSpeedOverride.GetValue();
	}
	else if (const USharedChaosCharacterMovementSettings* SharedSettingsPtr = SharedSettings.Get())
	{
		return SharedSettingsPtr->MaxSpeed;
	}

	UE_LOG(LogChaosMover, Warning, TEXT("Invalid max speed on ChaosCharacterMoverComponent"));
	return 0.0f;
}

void UChaosCharacterMovementMode::OverrideMaxSpeed(float Value)
{
	MaxSpeedOverride = Value;
}

void UChaosCharacterMovementMode::ClearMaxSpeedOverride()
{
	MaxSpeedOverride.Reset();
}

float UChaosCharacterMovementMode::GetAcceleration() const
{
	if (AccelerationOverride.IsSet())
	{
		return AccelerationOverride.GetValue();
	}
	else if (const USharedChaosCharacterMovementSettings* SharedSettingsPtr = SharedSettings.Get())
	{
		return SharedSettingsPtr->Acceleration;
	}
	
	UE_LOG(LogChaosMover, Warning, TEXT("Invalid acceleration on ChaosCharacterMoverComponent"));
	return 0.0f;
}

void UChaosCharacterMovementMode::OverrideAcceleration(float Value)
{
	AccelerationOverride = Value;
}

void UChaosCharacterMovementMode::ClearAccelerationOverride()
{
	AccelerationOverride.Reset();
}

void UChaosCharacterMovementMode::ModifyContacts(const FMoverTimeStep& TimeStep, const UE::ChaosMover::FSimulationInputData& InputData, const UE::ChaosMover::FSimulationOutputData& OutputData, Chaos::FCollisionContactModifier& Modifier) const
{
	Super::ModifyContacts(TimeStep, InputData, OutputData, Modifier);

	if (FrictionOverrideMode == ECharacterMoverFrictionOverrideMode::DoNotOverride)
	{
		return;
	}

	check(Simulation);

	// Get the updated (character) particle
	Chaos::FGeometryParticleHandle* UpdatedParticle = nullptr;
	const FChaosMoverSimulationDefaultInputs* SimInputs = Simulation->GetLocalSimInput().FindDataByType<FChaosMoverSimulationDefaultInputs>();
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
		bool bOverrideToZero = false;

		switch (FrictionOverrideMode)
		{
		case ECharacterMoverFrictionOverrideMode::AlwaysOverrideToZero:
			bOverrideToZero = true;
			break;
		case ECharacterMoverFrictionOverrideMode::OverrideToZeroWhenMoving:
		{
			const FCharacterDefaultInputs* CharacterInputs = InputData.InputCmd.InputCollection.FindDataByType<FCharacterDefaultInputs>();
			if (CharacterInputs)
			{
				constexpr float MinInput = 0.1f;
				bOverrideToZero = CharacterInputs->GetMoveInput().SizeSquared() > MinInput* MinInput;
			}
			break;
		}
			
		default:
			break;
		}

		if (bOverrideToZero)
		{
			PairModifier.ModifyStaticFriction(0.0f);
			PairModifier.ModifyDynamicFriction(0.0f);
		}
	}
}

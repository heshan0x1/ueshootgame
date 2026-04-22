// Copyright Epic Games, Inc. All Rights Reserved.

#include "PhysicsMover/MovementModifiers/PhysicsStanceModifier.h"

#include "Chaos/ParticleHandle.h"
#include "Chaos/Capsule.h"
#include "Components/CapsuleComponent.h"
#include "DefaultMovementSet/CharacterMoverComponent.h"
#include "DefaultMovementSet/Settings/StanceSettings.h"
#include "Engine/World.h"
#include "MoveLibrary/MovementUtils.h"
#include "PhysicsEngine/PhysicsObjectExternalInterface.h"
#include "PhysicsMover/Modes/PhysicsDrivenWalkingMode.h"
#include "PhysicsMover/Modes/PhysicsDrivenFallingMode.h"
#include "PhysicsMover/PhysicsMovementUtils.h"
#include "PBDRigidsSolver.h"
#include "Physics/Experimental/PhysScene_Chaos.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(PhysicsStanceModifier)


const Chaos::FCapsule FPhysicsStanceModifier::GenerateNewCapsule(float NewHalfHeight, float NewRadius, float NewGroundClearance, float TargetHeight)
{
	const float NewHeight = FMath::Max(2.0f * (NewHalfHeight - NewRadius), 0.0f);
	const Chaos::FVec3 Axis = Chaos::FVec3::UpVector;
	const Chaos::FVec3 NewX1 = (NewGroundClearance + NewRadius - TargetHeight) * Axis;
	const Chaos::FVec3 NewX2 = NewX1 + Axis * NewHeight;
	return Chaos::FCapsule(NewX1, NewX2, NewRadius);
}

void FPhysicsStanceModifier::UpdateCapsule(Chaos::FPBDRigidParticleHandle* ParticleHandle, float NewHalfHeight, float NewRadius, float TargetHeight, float GroundClearance)
{
	check(ParticleHandle);

	const Chaos::FCapsule NewCapsule = GenerateNewCapsule(NewHalfHeight, NewRadius, GroundClearance, TargetHeight);
	ParticleHandle->SetGeometry(Chaos::FImplicitObjectPtr(new Chaos::FCapsule(NewCapsule)));
	ParticleHandle->SetCenterOfMass(NewCapsule.GetCenterf());
}

void FPhysicsStanceModifier::ApplyModifierMove(UMoverComponent* MoverComp, const FMoverSyncState& InSyncState, float OriginalHalfHeight, float NewHalfHeight, float NewRadius)
{
	if (const UBaseMovementMode* MovementMode = MoverComp->FindMovementModeByName(InSyncState.MovementMode))
	{
		if (MovementMode->Implements<UPhysicsCharacterMovementModeInterface>())
		{
			if (const IPhysicsCharacterMovementModeInterface* PhysicsMode = CastChecked<IPhysicsCharacterMovementModeInterface>(MovementMode))
			{
				if (FMoverSyncState* MoverState = const_cast<FMoverSyncState*>(&InSyncState))
				{
					if (UPrimitiveComponent* PrimitiveComp = Cast<UPrimitiveComponent>(MoverComp->GetUpdatedComponent()))
					{
						// Update the character particle shape
						if (Chaos::FPBDRigidParticleHandle* ParticleHandle = UPhysicsMovementUtils::GetRigidParticleHandleFromComponent(PrimitiveComp))
						{
							// Invalidate particle before changing collision geometry (matches Chaos stance flow)
							if (const UWorld* World = MoverComp->GetWorld())
							{
								if (FPhysScene* Scene = World->GetPhysicsScene())
								{
									if (Chaos::FPhysicsSolver* Solver = Scene->GetSolver())
									{
										if (Solver->GetEvolution())
										{
											Solver->GetEvolution()->InvalidateParticle(ParticleHandle);
										}
									}
								}
							}

							const float TargetHeight = PhysicsMode->GetTargetHeight();
							UpdateCapsule(ParticleHandle, NewHalfHeight, NewRadius, TargetHeight, TargetHeight - OriginalHalfHeight);
						}
					}
				}
			}
		}
	}
}

void FPhysicsStanceModifier::OnStart(UMoverComponent* MoverComp, const FMoverTimeStep& TimeStep, const FMoverSyncState& SyncState, const FMoverAuxStateContext& AuxState)
{
	if (UStanceSettings* StanceSettings = MoverComp->FindSharedSettings_Mutable<UStanceSettings>())
	{
		if (const UCapsuleComponent* CapsuleComponent = Cast<UCapsuleComponent>(MoverComp->GetUpdatedComponent()))
		{
			if (const UCapsuleComponent* OriginalCapsule = UMovementUtils::GetOriginalComponentType<UCapsuleComponent>(MoverComp->GetOwner()))
			{
				const float OriginalHalfHeight = OriginalCapsule->GetScaledCapsuleHalfHeight();
				const float CrouchedHalfHeight = StanceSettings->CrouchHalfHeight;
				const float OriginalRadius = OriginalCapsule->GetScaledCapsuleRadius();

				ApplyModifierMove(MoverComp, SyncState, OriginalHalfHeight, CrouchedHalfHeight, OriginalRadius);

				ApplyMovementSettings(MoverComp);
			}
		}
	}

	// Ensures crouching, especially if triggered through state syncing (rollbacks, etc.)  
	if (UCharacterMoverComponent* CharMoverComp = Cast<UCharacterMoverComponent>(MoverComp))
	{
		CharMoverComp->Crouch();
	}
}

void FPhysicsStanceModifier::OnEnd(UMoverComponent* MoverComp, const FMoverTimeStep& TimeStep, const FMoverSyncState& SyncState, const FMoverAuxStateContext& AuxState)
{
	if (UStanceSettings* StanceSettings = MoverComp->FindSharedSettings_Mutable<UStanceSettings>())
	{
		if (UCapsuleComponent* CapsuleComponent = Cast<UCapsuleComponent>(MoverComp->GetUpdatedComponent()))
		{
			if (const UCapsuleComponent* OriginalCapsule = UMovementUtils::GetOriginalComponentType<UCapsuleComponent>(MoverComp->GetOwner()))
			{
				const float OriginalHalfHeight = OriginalCapsule->GetScaledCapsuleHalfHeight();
				const float OriginalRadius = OriginalCapsule->GetScaledCapsuleRadius();

				ApplyModifierMove(MoverComp, SyncState, OriginalHalfHeight, OriginalHalfHeight, OriginalRadius);

				RevertMovementSettings(MoverComp);
			}
		}
	}

	// Ensures uncrouching, especially if triggered through state syncing (rollbacks, etc.)  
	if (UCharacterMoverComponent* CharMoverComp = Cast<UCharacterMoverComponent>(MoverComp))
	{
		CharMoverComp->UnCrouch();
	}
}

bool FPhysicsStanceModifier::CanExpand_Internal(UMoverComponent* MoverComponent, USceneComponent* UpdatedComp, const FMoverSyncState& SyncState) const
{
	float StandingHalfHeight = 90;
	float CurrentHalfHeight = 55;

	bool bEncroached = true;

	if (const UCharacterMoverComponent* CharMoverComp = Cast<UCharacterMoverComponent>(MoverComponent))
	{
		const USceneComponent* UpdatedComponent = UpdatedComp;
		const UPrimitiveComponent* UpdatedCompAsPrimitive = Cast<UPrimitiveComponent>(UpdatedComponent);

		if (const UCapsuleComponent* OriginalCapsule = UMovementUtils::GetOriginalComponentType<UCapsuleComponent>(CharMoverComp->GetOwner()))
		{
			StandingHalfHeight = OriginalCapsule->GetScaledCapsuleHalfHeight();
		}

		if (const UCapsuleComponent* CapsuleComponent = Cast<UCapsuleComponent>(UpdatedCompAsPrimitive))
		{
			CurrentHalfHeight = CapsuleComponent->GetScaledCapsuleHalfHeight();
		}

		const float HalfHeightDifference = StandingHalfHeight - CurrentHalfHeight;

		const FMoverDefaultSyncState* DefaultSyncState = SyncState.SyncStateCollection.FindDataByType<FMoverDefaultSyncState>();

		const FVector PawnLocation = DefaultSyncState->GetLocation_WorldSpace();
		const FQuat PawnRot = DefaultSyncState->GetOrientation_WorldSpace().Quaternion();
		float PawnRadius = 0.0f;
		float PawnHalfHeight = 0.0f;
		UpdatedCompAsPrimitive->CalcBoundingCylinder(PawnRadius, PawnHalfHeight);

		// TODO: Compensate for the difference between current capsule size and standing size
		const FCollisionShape StandingCapsuleShape = FCollisionShape::MakeCapsule(PawnRadius, StandingHalfHeight);
		const ECollisionChannel CollisionChannel = UpdatedCompAsPrimitive->GetCollisionObjectType();

		// TODO: @Harsha Switch to physics thread safe IsOnGround_Internal() method when available.
		bool bShouldMaintainBase = false;
		if (const TObjectPtr<UBaseMovementMode>* CurrentMode = CharMoverComp->MovementModes.Find(SyncState.MovementMode))
		{
			const UBaseMovementMode* ActiveMode = CurrentMode->Get();
			if (ActiveMode && ActiveMode->HasGameplayTag(Mover_IsOnGround, true))
			{
				bShouldMaintainBase = true;
			}
		}

		if (!bShouldMaintainBase)
		{
			// Expand in place
			bEncroached = UMovementUtils::OverlapTest(UpdatedComponent, UpdatedCompAsPrimitive, PawnLocation, PawnRot, CollisionChannel, StandingCapsuleShape, CharMoverComp->GetOwner());
		}
		else
		{
			// Expand while keeping base location the same.
			const FVector StandingLocation = PawnLocation + (HalfHeightDifference + .01f) * FVector::UpVector;
			bEncroached = UMovementUtils::OverlapTest(UpdatedComponent, UpdatedCompAsPrimitive, StandingLocation, PawnRot, CollisionChannel, StandingCapsuleShape, CharMoverComp->GetOwner());
		}
	}
	return !bEncroached;
}

void FPhysicsStanceModifier::OnPostSimulationTick(const FStanceModifier* Modifier, UMoverComponent* MoverComp, UPrimitiveComponent* UpdatedPrimitive,  bool bIsCrouching, bool& bPostProcessed, OUT bool& bStanceChanged)
{
	if(const UStanceSettings* StanceSettings = MoverComp->FindSharedSettings<UStanceSettings>())
	{
		if (UCapsuleComponent* CapsuleComponent = Cast<UCapsuleComponent>(UpdatedPrimitive))
		{
			if (const UCapsuleComponent* OriginalCapsule = UMovementUtils::GetOriginalComponentType<UCapsuleComponent>(MoverComp->GetOwner()))
			{
				if (Modifier && bIsCrouching && !bPostProcessed) // Crouching
				{
					bStanceChanged = true;
					bPostProcessed = true;
				}
				else if (!Modifier && !bIsCrouching && bPostProcessed) // Uncrouching
				{
					bStanceChanged = true;
					bPostProcessed = false;
				}

				if (bStanceChanged)
				{
					float NewRadius = OriginalCapsule->GetScaledCapsuleRadius();
					float OriginalHalfHeight = OriginalCapsule->GetScaledCapsuleHalfHeight();
					float NewHalfHeight = bIsCrouching ? StanceSettings->CrouchHalfHeight : OriginalCapsule->GetScaledCapsuleHalfHeight();
					float TargetHeight = NewHalfHeight;

					if (const UBaseMovementMode* MovementMode = MoverComp->FindMovementModeByName(MoverComp->GetSyncState().MovementMode))
					{
						if (const IPhysicsCharacterMovementModeInterface* PhysicsMode = CastChecked<IPhysicsCharacterMovementModeInterface>(MovementMode))
						{
							TargetHeight = PhysicsMode->GetTargetHeight();
						}
					}

					const Chaos::FCapsule GeneratedCapsule = GenerateNewCapsule(NewHalfHeight, NewRadius, TargetHeight - OriginalHalfHeight, TargetHeight);
					Chaos::FImplicitObjectPtr NewCapsule(new Chaos::FCapsule(GeneratedCapsule));

					if (NewCapsule.IsValid())
					{
						if (IPhysicsComponent* PhysicsComponent = Cast<IPhysicsComponent>(UpdatedPrimitive))
						{
							if (Chaos::FPhysicsObject* PhysicsObject = PhysicsComponent->GetPhysicsObjectByName(NAME_None))
							{
								if (Chaos::FPBDRigidParticle* Particle = FPhysicsObjectExternalInterface::LockRead(PhysicsObject)->GetRigidParticle(PhysicsObject))
								{
									Particle->SetGeometry(NewCapsule);
								}
							}
						}
					}
				}
			}
		}
	}
}

FMovementModifierBase* FPhysicsStanceModifier::Clone() const
{
	FPhysicsStanceModifier* CopyPtr = new FPhysicsStanceModifier(*this);
	return CopyPtr;
}

void FPhysicsStanceModifier::NetSerialize(FArchive& Ar)
{
	Super::NetSerialize(Ar);
}

UScriptStruct* FPhysicsStanceModifier::GetScriptStruct() const
{
	return FPhysicsStanceModifier::StaticStruct();
}

FString FPhysicsStanceModifier::ToSimpleString() const
{
	return FString::Printf(TEXT("Physics Based Stance Modifier"));
}

void FPhysicsStanceModifier::AddReferencedObjects(FReferenceCollector& Collector)
{
	Super::AddReferencedObjects(Collector);
}

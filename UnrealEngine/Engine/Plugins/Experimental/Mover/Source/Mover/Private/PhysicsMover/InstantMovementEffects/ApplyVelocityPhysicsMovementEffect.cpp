// Copyright Epic Games, Inc. All Rights Reserved.

#include "PhysicsMover/InstantMovementEffects/ApplyVelocityPhysicsMovementEffect.h"

#include "Chaos/ParticleHandle.h"
#include "MoverComponent.h"
#include "MoverDataModelTypes.h"
#include "MoverSimulationTypes.h"
#include "MoveLibrary/MoverBlackboard.h"
#include "PhysicsMover/PhysicsMovementUtils.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(ApplyVelocityPhysicsMovementEffect)

FApplyVelocityPhysicsEffect::FApplyVelocityPhysicsEffect()
	: VelocityToApply(FVector::ZeroVector)
	, bAdditiveVelocity(false)
	, ForceMovementMode(NAME_None)
{
}

bool FApplyVelocityPhysicsEffect::ApplyMovementEffect(FApplyMovementEffectParams& ApplyEffectParams, FMoverSyncState& OutputState)
{
	// Get the position and orientation. Start by looking in the sync state.
	// If not there, get from the particle corresponding to the updated component
	FVector Position;
	FRotator Orientation;
	FVector AngularVelocityDegrees = FVector::ZeroVector;
	if (const FMoverDefaultSyncState* CurrentSyncState = OutputState.SyncStateCollection.FindDataByType<FMoverDefaultSyncState>())
	{
		Position = CurrentSyncState->GetLocation_WorldSpace();
		Orientation = CurrentSyncState->GetOrientation_WorldSpace();
		AngularVelocityDegrees = CurrentSyncState->GetAngularVelocityDegrees_WorldSpace();
	}
	else if (Chaos::FPBDRigidParticleHandle* ParticleHandle = UPhysicsMovementUtils::GetRigidParticleHandleFromComponent(ApplyEffectParams.UpdatedPrimitive))
	{
		Position = ParticleHandle->GetX();
		Orientation = FRotator(ParticleHandle->GetR());
		AngularVelocityDegrees = FMath::RadiansToDegrees(ParticleHandle->GetW());
	}
	else
	{
		return false;
	}

	FMoverDefaultSyncState& OutputSyncState = OutputState.SyncStateCollection.FindOrAddMutableDataByType<FMoverDefaultSyncState>();

	OutputState.MovementMode = ForceMovementMode;

	FRelativeBaseInfo MovementBaseInfo;
	if (const UMoverBlackboard* SimBlackboard = ApplyEffectParams.MoverComp->GetSimBlackboard())
	{
		SimBlackboard->TryGet(CommonBlackboard::LastFoundDynamicMovementBase, MovementBaseInfo);
	}

	FVector Velocity = VelocityToApply;
	if (bAdditiveVelocity)
	{
		if (const FMoverDefaultSyncState* SyncState = ApplyEffectParams.StartState->SyncState.SyncStateCollection.FindDataByType<FMoverDefaultSyncState>())
		{
			Velocity += SyncState->GetVelocity_WorldSpace();
		}
	}

	OutputSyncState.SetTransforms_WorldSpace(
		Position,
		Orientation,
		Velocity,
		AngularVelocityDegrees,
		MovementBaseInfo.MovementBase.Get(),
		MovementBaseInfo.BoneName);

	return true;
}

FInstantMovementEffect* FApplyVelocityPhysicsEffect::Clone() const
{
	FApplyVelocityPhysicsEffect* CopyPtr = new FApplyVelocityPhysicsEffect(*this);
	return CopyPtr;
}

void FApplyVelocityPhysicsEffect::NetSerialize(FArchive& Ar)
{
	Super::NetSerialize(Ar);

	SerializePackedVector<10, 16>(VelocityToApply, Ar);
	Ar.SerializeBits(&bAdditiveVelocity, 1);

	bool bUsingForcedMovementMode = !ForceMovementMode.IsNone();
	Ar.SerializeBits(&bUsingForcedMovementMode, 1);

	if (bUsingForcedMovementMode)
	{
		Ar << ForceMovementMode;
	}
}

UScriptStruct* FApplyVelocityPhysicsEffect::GetScriptStruct() const
{
	return FApplyVelocityPhysicsEffect::StaticStruct();
}

FString FApplyVelocityPhysicsEffect::ToSimpleString() const
{
	return FString::Printf(TEXT("ApplyVelocity"));
}

void FApplyVelocityPhysicsEffect::AddReferencedObjects(FReferenceCollector& Collector)
{
	Super::AddReferencedObjects(Collector);
}

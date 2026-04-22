// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosMover/ChaosMoverSimulationTypes.h"
#include "ChaosMover/ChaosMoverSimulation.h"
#include "ChaosMover/ChaosMoverLog.h"
#include "ChaosMover/ChaosMoverConsoleVariables.h"
#include "Serialization/ArchiveCrc32.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(ChaosMoverSimulationTypes)

void FChaosMoverSimulationDefaultInputs::Reset()
{
	CollisionResponseParams = FCollisionResponseParams();
	CollisionQueryParams = FCollisionQueryParams();
	UpDir = FVector::UpVector;
	Gravity = -980.7 * UpDir;
	PhysicsObjectGravity = 0.0f;
	PawnCollisionHalfHeight = 40.0f;
	PawnCollisionRadius = 30.0f;
	PhysicsObject = nullptr;
	OwningActor = nullptr;
	World = nullptr;
	CollisionChannel = ECC_Pawn;
}

FMoverDataCollection& UE::ChaosMover::GetDebugSimData(UChaosMoverSimulation* Simulation)
{
	check(Simulation);
	return Simulation->GetDebugSimData();
}

bool FChaosMovementBasis::NetSerialize(FArchive & Ar, UPackageMap * Map, bool& bOutSuccess)
{
	//bool bHasBasisLocation = BasisLocation.IsNearlyZero();
	Ar << BasisLocation;
	Ar << BasisRotation;
	bOutSuccess = true;
	return true;
}

FMoverDataStructBase* FChaosMovementBasis::Clone() const
{
	return new FChaosMovementBasis(*this);
}

UScriptStruct* FChaosMovementBasis::GetScriptStruct() const
{
	return StaticStruct();
}

void FChaosMovementBasis::ToString(FAnsiStringBuilderBase& Out) const
{
	Out.Appendf("Movement Basis: X = %s, R = %s (as rotator = %s)\n", *BasisLocation.ToString(), *BasisRotation.ToString(), *FRotator(BasisRotation).ToString());
}

bool FChaosMovementBasis::ShouldReconcile(const FMoverDataStructBase& AuthorityState) const
{
	const FChaosMovementBasis& TypedAuthority = static_cast<const FChaosMovementBasis&>(AuthorityState);
	static float LocationTolerance = 1e-3f;
	static float AngularTolerance = 1e-3f;
	return !BasisLocation.Equals(TypedAuthority.BasisLocation, LocationTolerance)
		|| !FMath::IsNearlyZero(BasisRotation.AngularDistance(TypedAuthority.BasisRotation), AngularTolerance);
}

void FChaosMovementBasis::Interpolate(const FMoverDataStructBase& From, const FMoverDataStructBase& To, float Pct)
{
	*this = static_cast<const FChaosMovementBasis&>((Pct < .5f) ? From : To);
}

void FChaosMovementBasis::Merge(const FMoverDataStructBase& From)
{
	
}

void FChaosMoverTimeStepDebugData::SetTimeStep(const FMoverTimeStep& InTimeStep)
{
	TimeStep = InTimeStep;
	bIsResimulating = InTimeStep.bIsResimulating;
}

FMoverDataStructBase* FChaosMoverTimeStepDebugData::Clone() const
{
	return new FChaosMoverTimeStepDebugData(*this);
}

UScriptStruct* FChaosMoverTimeStepDebugData::GetScriptStruct() const
{
	return StaticStruct();
}

void FChaosNetInstantMovementEffectsQueue::Add(const FScheduledInstantMovementEffect& InScheduledEffect, int32 IssuanceServerFrame, uint8 UniqueID)
{
	FChaosNetInstantMovementEffect& AddedInstancedEffect = Effects.AddDefaulted_GetRef();
	AddedInstancedEffect.ExecutionServerFrame = InScheduledEffect.ExecutionServerFrame;
	AddedInstancedEffect.IssuanceServerFrame = IssuanceServerFrame;	
	AddedInstancedEffect.UniqueID = UniqueID;
	TCheckedObjPtr<FInstantMovementEffect> InstantMovementEffect = InScheduledEffect.Effect.Get();
	AddedInstancedEffect.Effect.InitializeAsScriptStruct(InstantMovementEffect->GetScriptStruct(), (const uint8*)InScheduledEffect.Effect.Get());
}

bool FChaosNetInstantMovementEffectsQueue::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess)
{
	bOutSuccess = true;

	uint8 NumEffectsToSerialize;
	if (Ar.IsSaving())
	{
		NumEffectsToSerialize = Effects.Num();
	}
	Ar << NumEffectsToSerialize;

	if (Ar.IsLoading())
	{
		Effects.SetNumZeroed(NumEffectsToSerialize);
	}

	for (int32 EffectIndex = 0; EffectIndex < NumEffectsToSerialize && !Ar.IsError(); ++EffectIndex)
	{
		FChaosNetInstantMovementEffect& ScheduledEffect = Effects[EffectIndex];
		FInstantMovementEffect* Effect = ScheduledEffect.Effect.IsValid() ? &ScheduledEffect.Effect.GetMutable() : nullptr;
		TCheckedObjPtr<UScriptStruct> ScriptStruct = Ar.IsLoading() ? FInstantMovementEffect::StaticStruct() : (Effect ? Effect->GetScriptStruct() : nullptr);

		Ar << ScriptStruct;

		if (ScriptStruct.IsValid())
		{
			// Restrict replication to derived classes of FInstantMovementEffect for security reasons:
			// If FInstantMovementEffectsQueue is replicated through a Server RPC, we need to prevent clients from sending us
			// arbitrary ScriptStructs due to the allocation/reliance on GetCppStructOps below which could trigger a server crash
			// for invalid structs. All provided sources are direct children of FInstantMovementEffect and we never expect to have deep hierarchies
			// so this should not be too costly
			bool bIsDerivedFromBase = false;
			UStruct* CurrentSuperStruct = ScriptStruct->GetSuperStruct();
			while (CurrentSuperStruct)
			{
				if (CurrentSuperStruct == FInstantMovementEffect::StaticStruct())
				{
					bIsDerivedFromBase = true;
					break;
				}
				CurrentSuperStruct = CurrentSuperStruct->GetSuperStruct();
			}

			if (bIsDerivedFromBase)
			{
				Ar << ScheduledEffect.ExecutionServerFrame;
				Ar << ScheduledEffect.IssuanceServerFrame;
				Ar << ScheduledEffect.UniqueID;
				
				if (Ar.IsLoading())
				{
					ScheduledEffect.Effect.InitializeAsScriptStruct(ScriptStruct.Get());
				}

				FInstantMovementEffect& InstantMovementEffect = ScheduledEffect.Effect.GetMutable();
				InstantMovementEffect.NetSerialize(Ar);
			}
			else
			{
				UE_LOG(LogMover, Error, TEXT("FInstantMovementEffectsQueue::NetSerialize: ScriptStruct not derived from FInstantMovementEffect attempted to serialize."));
				Ar.SetError();
				bOutSuccess = false;
				return false;
			}
		}
		else if (ScriptStruct.IsError())
		{
			UE_LOG(LogMover, Error, TEXT("FInstantMovementEffectsQueue::NetSerialize: Invalid ScriptStruct serialized."));
			Ar.SetError();
			bOutSuccess = false;
			return false;
		}
	}

	return true;
}

FMoverDataStructBase* FChaosNetInstantMovementEffectsQueue::Clone() const
{
	FChaosNetInstantMovementEffectsQueue* CopyPtr = new FChaosNetInstantMovementEffectsQueue(*this);
	return CopyPtr;
}

void FChaosNetInstantMovementEffectsQueue::ToString(FAnsiStringBuilderBase& Out) const
{
	Out.Appendf("Instant Movement Effects Queue -------------------------------------------------\n");
	for (int32 EffectIndex = 0; EffectIndex < Effects.Num(); ++EffectIndex)
	{
		const FChaosNetInstantMovementEffect& ScheduledEffect = Effects[EffectIndex];
		Out.Appendf("Effect Index %d: ServerFrame %d, ", EffectIndex, ScheduledEffect.ExecutionServerFrame, ScheduledEffect.Effect.IsValid() ? *ScheduledEffect.Effect.Get().ToSimpleString() : TEXT("INVALID INSTANCED STRUCT EFFECT") );
		Out.AppendChar('\n');
	}
	Out.Appendf("--------------------------------------------------------------------------------\n");
}

void FChaosNetInstantMovementEffectsQueue::Interpolate(const FMoverDataStructBase& From, const FMoverDataStructBase& To, float Pct)
{
	const FChaosNetInstantMovementEffectsQueue& TypedFrom = static_cast<const FChaosNetInstantMovementEffectsQueue&>(From);
	const FChaosNetInstantMovementEffectsQueue& TypedTo = static_cast<const FChaosNetInstantMovementEffectsQueue&>(To);

	Effects = (Pct < 0.5f) ? TypedFrom.Effects : TypedTo.Effects;
}

UScriptStruct* FChaosNetInstantMovementEffectsQueue::GetScriptStruct() const
{
	return StaticStruct();
}

bool FChaosNetInstantMovementEffectsQueue::ShouldReconcile(const FMoverDataStructBase& AuthorityState) const
{
	const FChaosNetInstantMovementEffectsQueue& TypedAuthority = static_cast<const FChaosNetInstantMovementEffectsQueue&>(AuthorityState);
	if (Effects.Num() != TypedAuthority.Effects.Num())
	{
		return true;
	}

	for (int EffectIndex = 0; EffectIndex < Effects.Num(); ++EffectIndex)
	{
		const FChaosNetInstantMovementEffect& ScheduledEffect = Effects[EffectIndex];
		const FChaosNetInstantMovementEffect& AuthorityScheduledEffect = TypedAuthority.Effects[EffectIndex];

		if (ScheduledEffect.ExecutionServerFrame != AuthorityScheduledEffect.ExecutionServerFrame)
		{
			return true;
		}

		if (ScheduledEffect.Effect.IsValid() != AuthorityScheduledEffect.Effect.IsValid())
		{
			return true;
		}

		if (ScheduledEffect.Effect.IsValid())
		{
			if (ScheduledEffect.Effect.GetScriptStruct() != AuthorityScheduledEffect.Effect.GetScriptStruct())
			{
				return true;
			}

			// This allows us to skip implementing operator== for all movement effects
			FArchiveCrc32 Crc1;
			(const_cast<FChaosNetInstantMovementEffect&>(ScheduledEffect)).Effect.GetMutable().NetSerialize(Crc1);
			FArchiveCrc32 Crc2;
			(const_cast<FChaosNetInstantMovementEffect&>(AuthorityScheduledEffect)).Effect.GetMutable().NetSerialize(Crc2);
			bool bAreEqual = (Crc1.GetCrc() == Crc2.GetCrc());
			if (!bAreEqual)
			{
				return true;
			}
		}
	}

	return false;
}

void FChaosNetInstantMovementEffectsQueue::Merge(const FMoverDataStructBase& From)
{
	const FChaosNetInstantMovementEffectsQueue& TypedFrom = static_cast<const FChaosNetInstantMovementEffectsQueue&>(From);
	Effects.Append(TypedFrom.Effects);
}

void FChaosNetInstantMovementEffectsQueue::Decay(float DecayAmount)
{

}

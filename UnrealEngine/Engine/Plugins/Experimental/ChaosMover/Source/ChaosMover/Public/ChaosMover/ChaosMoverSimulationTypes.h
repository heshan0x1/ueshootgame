// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Chaos/Declares.h"
#include "Chaos/ImplicitObject.h"
#include "Chaos/PhysicsObject.h"
#include "CollisionQueryParams.h"
#include "DefaultMovementSet/MovementModifiers/StanceModifier.h"
#include "MoverSimulationTypes.h"
#include "StructUtils/InstancedStruct.h"

#include "ChaosMoverSimulationTypes.generated.h"

#define UE_API CHAOSMOVER_API

namespace Chaos
{
	class FPBDJointSettings;
	class FCharacterGroundConstraintSettings;
}

struct FConstraintProfileProperties;
class UChaosMoverSimulation;
class UBaseMovementMode;

namespace UE::ChaosMover
{
	struct FSimulationInputData
	{
		void Reset()
		{
			InputCmd.Reset();
			AuxInputState.AuxStateCollection.Empty();
		}

		mutable FMoverInputCmdContext InputCmd; // Can be overridden by network physics
		FMoverAuxStateContext AuxInputState; // Optional aux input state. Not networked
	};

	using FSimulationOutputData = ::UE::Mover::FSimulationOutputData;

	/** Mode change event structure, used to postpone callbacks to gameplay code when a mode has changed */
	struct FMovementModeChangeEvent
	{
		FName PreviousMovementModeName;
		TWeakObjectPtr<UBaseMovementMode> PreviousMovementMode;
		FName NextMovementModeName;
		TWeakObjectPtr<UBaseMovementMode> NextMovementMode;
	};

	// Util function to be able to get the debug sim data collection from a UChaosMoverSimulation from another plugin,
	// without including ChaosMoverSimulation.h
	CHAOSMOVER_API FMoverDataCollection& GetDebugSimData(UChaosMoverSimulation* Simulation);

} // namespace UE::ChaosMover

// Default chaos mover character simulation state, contains state basic for character simulation
USTRUCT(BlueprintType)
struct FChaosMoverCharacterSimState : public FMoverDataStructBase
{
	GENERATED_BODY()

	/** return newly allocated copy of this FMoverDataStructBase. MUST be overridden by derived types. */
	virtual FMoverDataStructBase* Clone() const override
	{
		return new FChaosMoverCharacterSimState(*this);
	}

	/** Gets the type info of this FMoverDataStructBase. MUST be overridden by derived types. */
	virtual UScriptStruct* GetScriptStruct() const override
	{
		return StaticStruct();
	}

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category=ChaosMover)
	FVector TargetDeltaPosition = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category=ChaosMover)
	float TargetDeltaFacing = 0.0f;
};

// Mover ground state, holds movement properties relative to the ground
USTRUCT(BlueprintType)
struct FChaosMoverGroundSimState : public FMoverDataStructBase
{
	GENERATED_BODY()

	/** return newly allocated copy of this FMoverDataStructBase. MUST be overridden by derived types. */
	virtual FMoverDataStructBase* Clone() const override
	{
		return new FChaosMoverGroundSimState(*this);
	}

	/** Gets the type info of this FMoverDataStructBase. MUST be overridden by derived types. */
	virtual UScriptStruct* GetScriptStruct() const override
	{
		return StaticStruct();
	}

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = ChaosMover)
	FVector LocalVelocity = FVector::ZeroVector;
};

// Movement basis state, for any movement that is relative to a basis transform given in world coordinates
USTRUCT(BlueprintType)
struct FChaosMovementBasis : public FMoverDataStructBase
{
	GENERATED_BODY()

	// Implementation of FMoverDataStructBase
	UE_API virtual bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess) override;
	UE_API virtual FMoverDataStructBase* Clone() const override;
	UE_API virtual UScriptStruct* GetScriptStruct() const override;
	UE_API virtual void ToString(FAnsiStringBuilderBase& Out) const override;
	UE_API virtual bool ShouldReconcile(const FMoverDataStructBase& AuthorityState) const override;
	UE_API virtual void Interpolate(const FMoverDataStructBase& From, const FMoverDataStructBase& To, float Pct) override;
	UE_API virtual void Merge(const FMoverDataStructBase& From) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = ChaosMover)
	FVector BasisLocation = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = ChaosMover)
	FQuat BasisRotation = FQuat::Identity;
};

// Data block containing all default inputs required by the Chaos Mover simulation
USTRUCT(BlueprintType)
struct FChaosMoverSimulationDefaultInputs : public FMoverDataStructBase
{
	GENERATED_BODY()

	FChaosMoverSimulationDefaultInputs()
	{
		Reset();
	}

	UE_API void Reset();

	/** return newly allocated copy of this FMoverDataStructBase. MUST be overridden by derived types. */
	virtual FMoverDataStructBase* Clone() const override
	{
		return new FChaosMoverSimulationDefaultInputs(*this);
	}

	/** Gets the type info of this FMoverDataStructBase. MUST be overridden by derived types. */
	virtual UScriptStruct* GetScriptStruct() const override
	{
		return StaticStruct();
	}

	FCollisionResponseParams CollisionResponseParams;
	FCollisionQueryParams CollisionQueryParams;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category=ChaosMover)
	FVector UpDir;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category=ChaosMover)
	FVector Gravity;

	// True if inputs are generated locally for this Actor
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category=ChaosMover)
	bool bIsGeneratingInputsLocally = false;

	// True if the Actor is a pawn, has a controller but that controller is not local
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category=ChaosMover)
	bool bIsRemotelyControlled = false;

	Chaos::FPhysicsObjectHandle PhysicsObject;
	AActor* OwningActor;
	UWorld* World;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category=ChaosMover)
	float PhysicsObjectGravity;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category=ChaosMover)
	float PawnCollisionHalfHeight;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category=ChaosMover)
	float PawnCollisionRadius;

	ECollisionChannel CollisionChannel;
};

// This will be replaced eventually by the Chaos Visual Debugger supporting the display of this information
USTRUCT()
struct FChaosMoverTimeStepDebugData : public FMoverDataStructBase
{
	GENERATED_BODY()

	virtual FMoverDataStructBase* Clone() const override;
	virtual UScriptStruct* GetScriptStruct() const override;

	void SetTimeStep(const FMoverTimeStep& InTimeStep);

	// This is so CVD can display TimeStep.bIsResimulating properly, which does not exist in FMoverTimeStep in a way that will show up by default
	UPROPERTY(VisibleAnywhere, Category = "Mover Info")
	bool bIsResimulating = false;

	UPROPERTY(VisibleAnywhere, Category = "Mover Info")
	FMoverTimeStep TimeStep;
};

UINTERFACE(BlueprintType, MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UChaosCharacterMovementModeInterface : public UInterface
{
	GENERATED_BODY()
};

class IChaosCharacterMovementModeInterface
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category=ChaosMover)
	virtual float GetTargetHeight() const = 0;

	UFUNCTION(BlueprintCallable, Category = ChaosMover)
	virtual float GetGroundQueryRadius() const = 0;

	UFUNCTION(BlueprintCallable, Category = ChaosMover)
	virtual float GetMaxWalkSlopeCosine() const = 0;

	UFUNCTION(BlueprintCallable, Category = ChaosMover)
	virtual bool ShouldCharacterRemainUpright() const = 0;

	UFUNCTION(BlueprintCallable, Category = ChaosMover)
	virtual float GetMaxSpeed() const = 0;

	UFUNCTION(BlueprintCallable, Category = ChaosMover)
	virtual void OverrideMaxSpeed(float Value) = 0;

	UFUNCTION(BlueprintCallable, Category = ChaosMover)
	virtual void ClearMaxSpeedOverride() = 0;

	UFUNCTION(BlueprintCallable, Category = ChaosMover)
	virtual float GetAcceleration() const = 0;

	UFUNCTION(BlueprintCallable, Category = ChaosMover)
	virtual void OverrideAcceleration(float Value) = 0;

	UFUNCTION(BlueprintCallable, Category = ChaosMover)
	virtual void ClearAccelerationOverride() = 0;
};

// Interface for mover modes moving on ground like characters, using a character ground constraint
UINTERFACE(MinimalAPI)
class UChaosCharacterConstraintMovementModeInterface : public UInterface
{
	GENERATED_BODY()
};

class IChaosCharacterConstraintMovementModeInterface
{
	GENERATED_BODY()

public:
	virtual float GetTargetHeight() const = 0;
	virtual bool ShouldEnableConstraint() const = 0;
	virtual void UpdateConstraintSettings(Chaos::FCharacterGroundConstraintSettings& ConstraintSettings) const = 0;
};

UENUM(BlueprintType)
enum class EChaosMoverVelocityEffectMode : uint8
{
	/** Apply as an additive impulse*/
	Impulse,

	/** Apply as an additive velocity */
	AdditiveVelocity,

	/** Apply as an override velocity */
	OverrideVelocity,
};

USTRUCT()
struct FStanceModifiedEventData : public FMoverSimulationEventData
{
	GENERATED_BODY()

	FStanceModifiedEventData(double InEventTimeMs, EStanceMode InOldStance, EStanceMode InNewStance, FEventProcessedCallbackPtr InEventProcessedCallback = nullptr)
		: FMoverSimulationEventData(InEventTimeMs, InEventProcessedCallback)
		, OldStance(InOldStance)
		, NewStance(InNewStance)
	{
	}
	FStanceModifiedEventData() {}

	virtual UScriptStruct* GetScriptStruct() const override
	{
		return FStanceModifiedEventData::StaticStruct();
	}

	EStanceMode OldStance = EStanceMode::Invalid;
	EStanceMode NewStance = EStanceMode::Invalid;
};

USTRUCT()
struct FStanceModifierModeChangedEventData : public FMoverSimulationEventData
{
	GENERATED_BODY()
	
	FStanceModifierModeChangedEventData(double InEventTimeMs, FName InOldMode, FName InNewMode, FEventProcessedCallbackPtr InEventProcessedCallback = nullptr)
	: FMoverSimulationEventData(InEventTimeMs, InEventProcessedCallback)
	, OldMode(InOldMode)
	, NewMode(InNewMode)
	{
	}
	FStanceModifierModeChangedEventData() {}

	virtual UScriptStruct* GetScriptStruct() const override
	{
		return FStanceModifierModeChangedEventData::StaticStruct();
	}

	FName OldMode = NAME_None;
	FName NewMode = NAME_None;
};

// Version of a FScheduledInstantMovementEffect with Issuance Server Frame
USTRUCT()
struct FChaosScheduledInstantMovementEffect
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category = "Mover")
	int32 IssuanceServerFrame = INDEX_NONE;

	// Whether this effect should rollback or not
	// We do not net serialize this value since any effect received from the network should always be rolled back
	// Only effects that were issued locally by the game thread should NOT roll back since resimulation does not run 
	// game thread logic again and will fail to generate them again
	UPROPERTY(VisibleAnywhere, Category = "Mover")
	bool bShouldRollBack = true;

	UPROPERTY(VisibleAnywhere, Category = ChaosMover)
	FScheduledInstantMovementEffect ScheduledEffect;
};

// Version of a FScheduledInstantMovementEffect with networkable instanced struct instead
USTRUCT()
struct FChaosNetInstantMovementEffect
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category = "Mover")
	int32 IssuanceServerFrame = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, Category = ChaosMover)
	int32 ExecutionServerFrame = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, Category = ChaosMover)
	uint8 UniqueID = 0xFF;

	// Whether this effect should rollback or not
	// We do not net serialize this value since any effect received from the network should always be rolled back
	// Only effects that were issued locally by the game thread should NOT roll back since resimulation does not run 
	// game thread logic again and will fail to generate them again
	UPROPERTY(VisibleAnywhere, Category = "Mover")
	bool bShouldRollBack = true;

	// If properties are added, FChaosNetInstantMovementEffectsQueue::NetSerialize should be updated to serialize them if necessary

	FScheduledInstantMovementEffect AsScheduledInstantMovementEffect() const
	{
		return FScheduledInstantMovementEffect(ExecutionServerFrame, /* ExecutionDateSeconds = */ 0.0, /* bIsAsyncMode = */ true, TSharedPtr<FInstantMovementEffect>(Effect.Get().Clone()));
	}

	// Eventually we want this to be a UPROPERTY so it can be displayed automatically in CVD
	// However this causes a crash when CVD tries to load this property in SerializeTaggedProperty because
	// it relies on the serialization of the struct class as a UObject, which is not supported by the type of archives used
	// to send and receive Mover debug data to CVD
	UPROPERTY(VisibleAnywhere, Category = ChaosMover)
	TInstancedStruct<FInstantMovementEffect> Effect;
};

USTRUCT()
struct FChaosNetInstantMovementEffectsQueue : public FMoverDataStructBase
{
	GENERATED_BODY()
	
	UPROPERTY(VisibleAnywhere, Category = ChaosMover)
	TArray<FChaosNetInstantMovementEffect> Effects;

	void Add(const FScheduledInstantMovementEffect& Effect, int32 IssuanceServerFrame, uint8 UniqueID);

	virtual FMoverDataStructBase* Clone() const override;
	virtual bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess) override;
	virtual UScriptStruct* GetScriptStruct() const override;
	virtual void ToString(FAnsiStringBuilderBase& Out) const override;
	virtual bool ShouldReconcile(const FMoverDataStructBase& AuthorityState) const override;
	virtual void Interpolate(const FMoverDataStructBase& From, const FMoverDataStructBase& To, float Pct) override;
	virtual void Merge(const FMoverDataStructBase& From) override;
	virtual void Decay(float DecayAmount) override;
};

#undef UE_API

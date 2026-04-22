// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once


#include "LayeredMove.h"
#include "DefaultMovementSet/LayeredMoves/MontageStateProvider.h"
#include "AnimRootMotionLayeredMove.generated.h"

#define UE_API MOVER_API

class UAnimMontage;


/** Anim Root Motion Move: handles root motion from a montage played on the primary visual component (skeletal mesh). 
 * In this method, root motion is extracted independently from anim playback. The move will end itself if the animation
 * is interrupted on the mesh.
 */
USTRUCT(BlueprintType)
struct FLayeredMove_AnimRootMotion : public FLayeredMove_MontageStateProvider
{
	GENERATED_BODY()

	UE_API FLayeredMove_AnimRootMotion();
	virtual ~FLayeredMove_AnimRootMotion() {}

	UPROPERTY(BlueprintReadWrite, Category = Mover)
	FMoverAnimMontageState MontageState;

	// Generate a movement 
	UE_API virtual bool GenerateMove(const FMoverTickStartData& StartState, const FMoverTimeStep& TimeStep, const UMoverComponent* MoverComp, UMoverBlackboard* SimBlackboard, FProposedMove& OutProposedMove) override;

	UE_API virtual FLayeredMoveBase* Clone() const override;

	UE_API virtual void NetSerialize(FArchive& Ar) override;

	UE_API virtual UScriptStruct* GetScriptStruct() const override;

	UE_API virtual FString ToSimpleString() const override;

	UE_API virtual void AddReferencedObjects(class FReferenceCollector& Collector) override;

	// FLayeredMove_MontageStateProvider
	UE_API virtual FMoverAnimMontageState GetMontageState() const override;
};

template<>
struct TStructOpsTypeTraits< FLayeredMove_AnimRootMotion > : public TStructOpsTypeTraitsBase2< FLayeredMove_AnimRootMotion >
{
	enum
	{
		WithCopy = true
	};
};

#undef UE_API

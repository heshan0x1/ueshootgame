// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once


#include "LayeredMove.h"
#include "MontageStateProvider.generated.h"

#define UE_API MOVER_API

class UAnimMontage;


/** Data about montages that is replicated to simulated clients */
USTRUCT(BlueprintType)
struct FMoverAnimMontageState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = Mover)
	TObjectPtr<UAnimMontage> Montage;

	// Montage position when started (in unscaled seconds). 
	UPROPERTY(BlueprintReadWrite, Category = Mover)
	float StartingMontagePosition = 0.0f;

	// Rate at which this montage is intended to play
	UPROPERTY(BlueprintReadWrite, Category = Mover)
	float PlayRate = 1.0f;

	// Current position (during playback only)
	UPROPERTY(BlueprintReadOnly, Category = Mover)
	float CurrentPosition = 0.0f;

	void Reset();
	void NetSerialize(FArchive& Ar);
};



/** Note this will become obsolete once layered move logic is represented by a uobject, allowing use of interface classes.  */
USTRUCT(BlueprintType)
struct FLayeredMove_MontageStateProvider : public FLayeredMoveBase
{
	GENERATED_BODY()

	virtual FMoverAnimMontageState GetMontageState() const
	{
		checkNoEntry();
		return FMoverAnimMontageState();
	}
};

#undef UE_API

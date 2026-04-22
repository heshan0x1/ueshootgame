// Copyright Epic Games, Inc. All Rights Reserved.

#include "DefaultMovementSet/LayeredMoves/MontageStateProvider.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MontageStateProvider)


void FMoverAnimMontageState::Reset()
{
	Montage = nullptr;
}

void FMoverAnimMontageState::NetSerialize(FArchive& Ar)
{
	Ar << Montage;

	uint8 bHasNonDefaultStartingPosition = 0;
	uint8 bHasNonDefaultPlayRate = 0;

	if (Ar.IsSaving())
	{
		bHasNonDefaultStartingPosition = (StartingMontagePosition != 0.0f);
		bHasNonDefaultPlayRate = (PlayRate != 1.0f);
	}

	Ar.SerializeBits(&bHasNonDefaultStartingPosition, 1);
	Ar.SerializeBits(&bHasNonDefaultPlayRate, 1);

	if (bHasNonDefaultStartingPosition)
	{
		Ar << StartingMontagePosition;
	}
	else
	{
		StartingMontagePosition = 0.0f;
	}

	if (bHasNonDefaultPlayRate)
	{
		Ar << PlayRate;
	}
	else
	{
		PlayRate = 1.0f;
	}

	Ar << CurrentPosition;
}

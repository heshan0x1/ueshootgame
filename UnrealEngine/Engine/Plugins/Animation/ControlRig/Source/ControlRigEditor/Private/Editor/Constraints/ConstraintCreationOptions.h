// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Constraints/MovieSceneConstraintChannelHelper.h"
#include "UObject/Object.h"

#include "ConstraintCreationOptions.generated.h"

/**
 * UConstraintCreationOptions is used to pass options for creating constraints.
 */

UCLASS(Blueprintable)
class UConstraintCreationOptions : public UObject
{
	GENERATED_BODY()

public:

	/* Creation options related to sequencer. */
	UPROPERTY(EditAnywhere, Category=Options)
	FSequencerCreationOptions SequencerOptions;
};

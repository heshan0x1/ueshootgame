// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "MassEntityElementTypes.h"
#include "FSimpleRandomMoveFragment.generated.h"

/**
 * 
 */
USTRUCT()
struct FSimpleRandomMoveFragment : public FMassFragment
{
	GENERATED_BODY()

	FVector Target = FVector::ZeroVector;
};

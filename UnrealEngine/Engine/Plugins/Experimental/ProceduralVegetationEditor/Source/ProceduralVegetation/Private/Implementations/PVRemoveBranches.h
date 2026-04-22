// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PVRemoveBranches.generated.h"

struct FManagedArrayCollection;

UENUM()
enum class ERemoveBranchesBasis : uint8
{
	Length,
	Radius,
	Light,
	Age,	
	Generation
};

struct FPVRemoveBranches
{
	static void ApplyRemoveBranches(const ERemoveBranchesBasis InBasis, const float InThreshold, FManagedArrayCollection& OutCollection);
};

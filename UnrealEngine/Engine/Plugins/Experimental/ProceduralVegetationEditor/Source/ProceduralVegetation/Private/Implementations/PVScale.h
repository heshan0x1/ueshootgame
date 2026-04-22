// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GeometryCollection/ManagedArrayCollection.h"

struct FPVScale
{
public:
	static void ApplyScale(const float InManualScale, FManagedArrayCollection& OutCollection);
};

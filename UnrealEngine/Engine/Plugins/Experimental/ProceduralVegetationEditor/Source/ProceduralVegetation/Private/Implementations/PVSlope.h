// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GeometryCollection/ManagedArrayCollection.h"
#include "PVSlope.generated.h"

namespace PV::Facades
{
	class FPointFacade;
	class FBranchFacade;
}

UENUM()
enum class EPVSlopeTrunkPivotPoint
{
	Origin,
	Trunk
};

USTRUCT()
struct FPVSlopeParams
{
	GENERATED_BODY()

	/** The angle of the ground. Measured in degrees. */
	UPROPERTY(EditAnywhere, Category = "Slope", meta = (Units = Degrees, ClampMin = -90.0f, ClampMax = 90.0f, UIMin = -90.0f, UIMax = 90.0f))
	float SlopeAngle = 22.5;

	/** The direction of the slope. Measured in degrees. */
	UPROPERTY(EditAnywhere, Category = "Slope", meta = (Units = Degrees, ClampMin = -180.0f, ClampMax = 180.0f, UIMin = -180.0f, UIMax = 180.0f))
	float SlopeDirection = 0;

	/** How strongly the vegetation should bend towards the sky as it grows. 0 = no bending. */
	UPROPERTY(EditAnywhere, Category = "Slope", meta = (ClampMin = 0.0f, ClampMax = 100.0f, UIMin = 0.0f, UIMax = 10.0f))
	float BendStrength = 2.0;

	/** The pivot point of the tunk. */
	UPROPERTY(EditAnywhere, Category = "Slope")
	EPVSlopeTrunkPivotPoint TrunkPivotPoint = EPVSlopeTrunkPivotPoint::Origin;
};

struct FPVSlope
{
	static void ApplySlope(const FPVSlopeParams& InSlopeParams, FManagedArrayCollection& OutCollection);
};

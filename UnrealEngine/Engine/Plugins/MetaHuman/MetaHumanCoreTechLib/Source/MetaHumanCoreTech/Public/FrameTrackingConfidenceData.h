// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "FrameTrackingConfidenceData.generated.h"


USTRUCT()
struct FFrameTrackingConfidenceData
{
	GENERATED_BODY()

	UPROPERTY()
	double Value = -1;
};

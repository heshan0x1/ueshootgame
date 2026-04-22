// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "TrackerOpticalFlowConfiguration.generated.h"

USTRUCT()
struct FTrackerOpticalFlowConfiguration
{
	GENERATED_BODY()

	UPROPERTY()
	bool bUseOpticalFlow{};

	UPROPERTY()
	bool bUseConfidence{};

	UPROPERTY()
	bool bUseForwardFlow{};
};

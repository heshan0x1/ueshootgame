// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "LoudnessMeterRackUnitSettings.generated.h"

USTRUCT()
struct FLoudnessMetricDisplayOptions
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, config, Category = LoudnessMeter)
	bool bShowValue = false;

	UPROPERTY(EditAnywhere, config, Category = LoudnessMeter)
	bool bShowMeter = false;
};

USTRUCT()
struct FLoudnessMeterRackUnitSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, config, Category = LoudnessMeter)
	bool bDisplayAnalysisTimer = true;

	UPROPERTY(EditAnywhere, config, Category = LoudnessMeter)
	FLoudnessMetricDisplayOptions LongTermLoudness = { .bShowValue = true, .bShowMeter = false };

	UPROPERTY(EditAnywhere, config, Category = LoudnessMeter)
	FLoudnessMetricDisplayOptions ShortTermLoudness = { .bShowValue = false, .bShowMeter = true };

	UPROPERTY(EditAnywhere, config, Category = LoudnessMeter)
	FLoudnessMetricDisplayOptions MomentaryLoudness = { .bShowValue = false, .bShowMeter = true };
};

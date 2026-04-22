// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CaptureData.h"

#include "MetaHumanCalibrationGeneratorConfig.h"
#include "MetaHumanCalibrationGeneratorOptions.h"

#include "MetaHumanCalibrationAutoFrameSelector.generated.h"

UCLASS(BlueprintType, Blueprintable)
class UMetaHumanCalibrationAutoFrameSelector : public UObject
{
	GENERATED_BODY()

public:

	UFUNCTION(BlueprintCallable, Category = "MetaHuman | Calibration Frame Selector")
	TArray<int32> Run(const UFootageCaptureData* InCaptureData, 
					  const UMetaHumanCalibrationGeneratorConfig* InConfig, 
					  const UMetaHumanCalibrationGeneratorOptions* InOptions);
};
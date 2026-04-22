// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/SharedPointer.h"

#include "MetaHumanStereoCalibrator.h"
#include "MetaHumanCalibrationGeneratorOptions.h"
#include "MetaHumanCalibrationGeneratorConfig.h"

#include "CaptureData.h"

#include "MetaHumanCalibrationGenerator.generated.h"

UCLASS(BlueprintType, Blueprintable)
class UMetaHumanCalibrationGenerator : public UObject
{
	GENERATED_BODY()

public:

	UMetaHumanCalibrationGenerator();

	UFUNCTION(BlueprintCallable, Category = "MetaHuman | Calibration Generator")
	bool Init(const UMetaHumanCalibrationGeneratorConfig* InConfig);

	UFUNCTION(BlueprintCallable, Category = "MetaHuman | Calibration Generator")
	bool ConfigureCameras(const UFootageCaptureData* InCaptureData);

	UFUNCTION(BlueprintCallable, Category = "MetaHuman | Calibration Generator")
	bool Process(UFootageCaptureData* InCaptureData, const UMetaHumanCalibrationGeneratorOptions* InOptions);

	UFUNCTION(BlueprintCallable, Category = "MetaHuman | Calibration Generator")
	double GetLastRMSError() const;

	bool Reset(const UMetaHumanCalibrationGeneratorConfig* InConfig, const UFootageCaptureData* InCaptureData);

private:

	TSharedPtr<UE::Wrappers::FMetaHumanStereoCalibrator> StereoCalibrator;
	double LastRMSError = 0.0f;
	bool bInitialized = false;
	bool bCamerasConfigured = false;
};

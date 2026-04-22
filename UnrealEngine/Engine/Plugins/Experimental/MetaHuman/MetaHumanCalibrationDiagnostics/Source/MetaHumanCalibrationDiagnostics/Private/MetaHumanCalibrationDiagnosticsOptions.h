// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Object.h"

#include "CameraCalibration.h"
#include "Layout/SlateRect.h"

#include "MetaHumanAreaOfInterest.h"

#include "MetaHumanCalibrationDiagnosticsOptions.generated.h"

/** Options that will used as part of the camera calibration diagnostics process */
UCLASS(BlueprintType, Blueprintable)
class UMetaHumanCalibrationDiagnosticsOptions
	: public UObject
{
public:

	GENERATED_BODY()

	/** Calibration that will be used to run the diagnostics */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Options")
	TObjectPtr<UCameraCalibration> CameraCalibration = nullptr;

	/** Area for which the calibration diagnostics will be presented */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Options")
	TArray<FMetaHumanAreaOfInterest> AreaOfInterestsForCameras;

	/** 
	* Maximum acceptable root mean square (RMS) reprojection error for calibration to be considered valid 
	*
	* Note: The points and lines drawn will be colored using an HSL gradient: 0 = green, 1 = red, with intermediate values transitioning smoothly.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Options", DisplayName = "RMS Error Threshold")
	double RMSErrorThreshold = 3.0;

	/** Maximum acceptable feature match reprojection error for which the detected points will be considered valid */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Options")
	double FeatureMatchErrorThreshold = 5.0;
	/** 
	* Maximum acceptable reprojection error for detected points to be considered valid 
	* 
	* Note: The points and lines drawn will be colored using an HSL gradient: 0 = green, 1 = red, with intermediate values transitioning smoothly.
	*/
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Options")
	double ReprojectionErrorThreshold = 3.0;

	UPROPERTY()
	double RatioThreshold = 0.75;
};
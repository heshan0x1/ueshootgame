// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Object.h"
#include "UObject/SoftObjectPath.h"

#include "Templates/ValueOrError.h"

#include "MetaHumanAreaOfInterest.h"

#include "MetaHumanCalibrationGeneratorOptions.generated.h"

/** Options that will used as part of the camera calibration process */
UCLASS(BlueprintType, Blueprintable)
class UMetaHumanCalibrationGeneratorOptions
	: public UObject
{
public:

	GENERATED_BODY()

	/** Name of the Camera Calibration asset. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Options", meta = (ContentDir))
	FString AssetName = TEXT("CC_Calibration");

	/** Content Browser path where the Lens Files and Camera Calibration assets will be created. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Options", meta = (ContentDir))
	FDirectoryPath PackagePath;

	/** Automatically save created assets. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Options")
	bool bAutoSaveAssets = true;

	/** Frames selected for the calibration process */
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Options")
	TArray<int32> SelectedFrames;

	/** Rate at which the camera calibration process will sample frames.
	* 
	* Example: 30 will use every 30th frame.
	* 
	* Note: Low sample rates will take longer for processing to complete.
	*/
	UE_DEPRECATED(5.7, "SampleRate is deprecated. Use automatic frame selection to obtain the list of frames.")
	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "SampleRate is deprecated. Use automatic frame selection to obtain the list of frames."))
	int32 SampleRate_DEPRECATED = 30;

	/** The width of the chessboard used to record the calibration footage. */
	UE_DEPRECATED(5.7, "Board Pattern Width is deprecated. Use UMetaHumanCalibrationGeneratorConfig object.")
	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Board Pattern Width is deprecated. Use UMetaHumanCalibrationGeneratorConfig object."))
	int32 BoardPatternWidth_DEPRECATED = 10;

	/** The width of the chessboard used to record the calibration footage. */
	UE_DEPRECATED(5.7, "Board Pattern Height is deprecated. Use UMetaHumanCalibrationGeneratorConfig object.")
	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Board Pattern Height is deprecated. Use UMetaHumanCalibrationGeneratorConfig object."))
	int32 BoardPatternHeight_DEPRECATED = 15;

	/** The square size of the chessboard used to record the calibration footage. */
	UE_DEPRECATED(5.7, "Board Square Size is deprecated. Use UMetaHumanCalibrationGeneratorConfig object.")
	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Board Square Size is deprecated. Use UMetaHumanCalibrationGeneratorConfig object."))
	float BoardSquareSize_DEPRECATED = 0.75f;

	/** Value represents the allowed blurriness (in pixels) of the frame that will be used for calibration process.
	* If the frame has estimated blurriness higher than this threshold, the frame will be discarded.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Options")
	float SharpnessThreshold = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Options")
	TArray<FMetaHumanAreaOfInterest> AreaOfInterestsForCameras;

	TValueOrError<void, FString> CheckOptionsValidity() const;

private:

	UFUNCTION(BlueprintCallable, Category = "Options")
	void SetSelectedFrames(TArray<int32> InSelectedFrames);

	UFUNCTION()
	bool IsSelectedFramesEmpty() const;
};
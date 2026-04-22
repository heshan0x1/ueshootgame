// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/SharedPointer.h"

#include "Containers/Map.h"

#include "MetaHumanStereoCalibrator.h"

class FMetaHumanCalibrationPatternDetector
{
private:

	struct FPrivateToken {};

public:

	using FDetectedFrame = TMap<FString, TArray<FVector2D>>;
	using FDetectedFrames = TArray<FDetectedFrame>;

	struct FPatternInfo
	{
		int32 Width;
		int32 Height;
		float SquareSize;
	};

	DECLARE_DELEGATE_OneParam(FProgressReporter, double);

	using FFramePaths = TPair<FString, FString>;
	DECLARE_DELEGATE_RetVal_TwoParams(TOptional<FFramePaths>, FOnFailureFrameProvider, const FFramePaths&, int32);

	static TUniquePtr<FMetaHumanCalibrationPatternDetector> CreateFromExistingCalibrator(TSharedPtr<UE::Wrappers::FMetaHumanStereoCalibrator> InStereoCalibrator);
	static TUniquePtr<FMetaHumanCalibrationPatternDetector> CreateFromNew(const FPatternInfo& InBoardInfo, const TMap<FString, FIntVector2>& InCameras, float InScale);

	FMetaHumanCalibrationPatternDetector(FPrivateToken, TSharedPtr<UE::Wrappers::FMetaHumanStereoCalibrator> InStereoCalibrator, TOptional<float> InScale = {});

	void SetOnProgressReporter(FProgressReporter InProgressReporter);

	FDetectedFrames DetectPatterns(const TPair<FString, FString>& InCameraNames,
								   const TPair<TArray<FString>, TArray<FString>>& InFilteredFrames,
								   float InSharpnessThreshold, 
								   FOnFailureFrameProvider InOnFailureFrameProvider = nullptr);

	TOptional<FDetectedFrame> DetectPattern(const TPair<FString, FString>& InCameraNames,
											const TPair<FString, FString>& InFramePath,
											float InSharpnessThreshold);

private:

	TSharedPtr<UE::Wrappers::FMetaHumanStereoCalibrator> StereoCalibrator;
	TOptional<float> Scale;

	FProgressReporter ProgressReporter;
};
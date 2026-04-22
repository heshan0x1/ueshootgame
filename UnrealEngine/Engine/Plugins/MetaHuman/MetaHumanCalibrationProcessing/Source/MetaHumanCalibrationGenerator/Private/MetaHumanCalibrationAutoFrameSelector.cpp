// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanCalibrationAutoFrameSelector.h"

#include "MetaHumanCalibrationPatternDetector.h"
#include "Utils/MetaHumanCalibrationAutoFrameSelection.h"
#include "Utils/MetaHumanCalibrationFrameResolver.h"

#include "ImageSequenceUtils.h"
#include "ImgMediaSource.h"

#include "Settings/MetaHumanCalibrationGeneratorSettings.h"

#include "Utils/MetaHumanCalibrationUtils.h"

DEFINE_LOG_CATEGORY_STATIC(LogMetaHumanCalibrationAutoFrameSelector, Log, All);

TArray<int32> UMetaHumanCalibrationAutoFrameSelector::Run(const UFootageCaptureData* InCaptureData,
														  const UMetaHumanCalibrationGeneratorConfig* InConfig,
														  const UMetaHumanCalibrationGeneratorOptions* InOptions)
{
	using namespace UE::MetaHuman::Image;

	if (!IsValid(InCaptureData))
	{
		UE_LOG(LogMetaHumanCalibrationAutoFrameSelector, Error, TEXT("Invalid Capture Data asset provided"));
		return TArray<int32>();
	}

	if (!IsValid(InConfig))
	{
		UE_LOG(LogMetaHumanCalibrationAutoFrameSelector, Error, TEXT("Invalid Config object provided"));
		return TArray<int32>();
	}

	TValueOrError<void, FString> ConfigValid = InConfig->CheckConfigValidity();
	if (ConfigValid.HasError())
	{
		UE_LOG(LogMetaHumanCalibrationAutoFrameSelector, Error, TEXT("Config doesn't pass validity check: %s"), *ConfigValid.GetError());
		return TArray<int32>();
	}

	if (!IsValid(InOptions))
	{
		UE_LOG(LogMetaHumanCalibrationAutoFrameSelector, Error, TEXT("Invalid Options object provided"));
		return TArray<int32>();
	}

	if (InCaptureData->ImageSequences.Num() < 2)
	{
		UE_LOG(LogMetaHumanCalibrationAutoFrameSelector, Error, TEXT("Stereo calibration process expects 2 cameras, but found %d"), InCaptureData->ImageSequences.Num());
		return TArray<int32>();
	}

	FMetaHumanCalibrationPatternDetector::FPatternInfo PatternInfo = {
		.Width = InConfig->BoardPatternWidth - 1,
		.Height = InConfig->BoardPatternHeight - 1,
		.SquareSize = InConfig->BoardSquareSize
	};

	TMap<FString, FIntVector2> Cameras;
	for (const UImgMediaSource* ImageSource : InCaptureData->ImageSequences)
	{
		if (!IsValid(ImageSource))
		{
			UE_LOG(LogMetaHumanCalibrationAutoFrameSelector, Error, TEXT("Invalid Image Media Source asset provided"));
			return TArray<int32>();
		}

		FIntVector2 ImageDimensions;
		int32 NumberOfImages = 0;
		FImageSequenceUtils::GetImageSequenceInfoFromAsset(ImageSource, ImageDimensions, NumberOfImages);

		Cameras.Add(ImageSource->GetName(), ImageDimensions);
	}
	
	const UMetaHumanCalibrationGeneratorSettings* Settings = GetDefault<UMetaHumanCalibrationGeneratorSettings>();

	TUniquePtr<FMetaHumanCalibrationPatternDetector> PatternDetector =
		FMetaHumanCalibrationPatternDetector::CreateFromNew(PatternInfo, Cameras, Settings->ScaleFactor);

	using FFrameCameraPaths = TPair<TArray<FString>, TArray<FString>>;
	FFrameCameraPaths CameraPaths;

	TOptional<FMetaHumanCalibrationFrameResolver> ResolverOpt = FMetaHumanCalibrationFrameResolver::CreateFromCaptureData(InCaptureData);
	if (!ResolverOpt.IsSet())
	{
		UE_LOG(LogMetaHumanCalibrationAutoFrameSelector, Error, TEXT("Frame Resolver is NOT valid."));
		return TArray<int32>();
	}

	FMetaHumanCalibrationFrameResolver Resolver = MoveTemp(ResolverOpt.GetValue());

	if (!Resolver.HasFrames())
	{
		UE_LOG(LogMetaHumanCalibrationAutoFrameSelector, Error, TEXT("No matching frames found."));
		return TArray<int32>();
	}

	Resolver.GetFramePathsForCameraIndex(0, CameraPaths.Key);
	Resolver.GetFramePathsForCameraIndex(1, CameraPaths.Value);

	TArray<int32> FilteredFrameIndices =
		FilterFrameIndices(CameraPaths, [SampleRate = Settings->AutomaticFrameSelectionSampleRate](int32 InFrame)
	{
		return InFrame % SampleRate == 0;
	});

	TArray<FString> CameraNamesArray;
	TArray<FIntVector2> DimensionsArray;
	Cameras.GenerateKeyArray(CameraNamesArray);
	Cameras.GenerateValueArray(DimensionsArray);

	TPair<FString, FString> CameraPair;
	CameraPair.Key = CameraNamesArray[0];
	CameraPair.Value = CameraNamesArray[1];

	TPair<FIntVector2, FIntVector2> DimensionsPair;
	DimensionsPair.Key = DimensionsArray[0];
	DimensionsPair.Value = DimensionsArray[1];

	TMap<int32, FMetaHumanCalibrationPatternDetector::FDetectedFrame> DetectedFrames;

	for (int32 FrameNumber : FilteredFrameIndices)
	{
		int32 FrameNumberToTry = FrameNumber;

		TPair<FString, FString> FramePath;
		FramePath.Key = CameraPaths.Key[FrameNumberToTry];
		FramePath.Value = CameraPaths.Value[FrameNumberToTry];

		TOptional<FMetaHumanCalibrationPatternDetector::FDetectedFrame> DetectedFrame = 
			PatternDetector->DetectPattern(CameraPair, FramePath, InOptions->SharpnessThreshold);

		if (!DetectedFrame.IsSet())
		{
			for (int32 TryAttempt = 1; TryAttempt <= 5; ++TryAttempt)
			{
				FrameNumberToTry = FrameNumber + TryAttempt;

				// Invalid index, nothing to retry
				if (!CameraPaths.Key.IsValidIndex(FrameNumberToTry) ||
					!CameraPaths.Value.IsValidIndex(FrameNumberToTry))
				{
					break;
				}

				// Valid index, but frame is already selected for a check
				if (FilteredFrameIndices.Contains(FrameNumberToTry))
				{
					break;
				}

				FramePath.Key = CameraPaths.Key[FrameNumberToTry];
				FramePath.Value = CameraPaths.Value[FrameNumberToTry];

				DetectedFrame = PatternDetector->DetectPattern(CameraPair, FramePath, InOptions->SharpnessThreshold);

				if (DetectedFrame.IsSet())
				{
					break;
				}
			}
		}

		if (DetectedFrame.IsSet())
		{
			DetectedFrames.Add(FrameNumberToTry, MoveTemp(DetectedFrame.GetValue()));
		}
	}

	TPair<FBox2D, FBox2D> AreaOfInterestPair;
	AreaOfInterestPair.Key = FBox2D(FVector2D::ZeroVector, FVector2D(DimensionsPair.Key));
	AreaOfInterestPair.Value = FBox2D(FVector2D::ZeroVector, FVector2D(DimensionsPair.Value));

	if (InOptions->AreaOfInterestsForCameras.Num() > 2)
	{
		AreaOfInterestPair.Key = InOptions->AreaOfInterestsForCameras[0].GetBox2D();
		AreaOfInterestPair.Value = InOptions->AreaOfInterestsForCameras[1].GetBox2D();
	}

	FMetaHumanCalibrationAutoFrameSelection FrameSelector(MoveTemp(CameraPair), MoveTemp(DimensionsPair), MoveTemp(AreaOfInterestPair));

	return FrameSelector.RunSelection(PatternInfo, DetectedFrames);
}

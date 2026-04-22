// Copyright Epic Games, Inc. All Rights Reserved.

#include "UMetaHumanRobustFeatureMatcher.h"

#include "ImageSequenceUtils.h"
#include "ImgMediaSource.h"

#include "IImageWrapperModule.h"
#include "Modules/ModuleManager.h"

#include "Misc/FileHelper.h"

#include "Utils/MetaHumanCalibrationUtils.h"

DEFINE_LOG_CATEGORY_STATIC(LogMetaHumanRobustFeatureMatcher, Log, All);

namespace Private
{

int32 GetCalibrationIndex(const FString& ViewName, TArray<FCameraCalibration> Calibrations)
{
	for (int32 Index = 0; Index < Calibrations.Num(); ++Index)
	{
		const FString& CameraName = Calibrations[Index].CameraId;

		if (ViewName.Contains(CameraName))
		{
			return Index;
		}
	}

	return INDEX_NONE;
}

}

bool FDetectedFeatures::IsValid() const
{
	if (FrameIndex == INDEX_NONE || Points3d.IsEmpty() || CameraPoints.IsEmpty() || Points3dReprojected.IsEmpty())
	{
		return false;
	}

	return true;
}

UMetaHumanRobustFeatureMatcher::UMetaHumanRobustFeatureMatcher()
	: FeatureMatcher(MakeShared<UE::Wrappers::FMetaHumanRobustFeatureMatcher>())
{
}

bool UMetaHumanRobustFeatureMatcher::Init(UFootageCaptureData* InCaptureData, UMetaHumanCalibrationDiagnosticsOptions* InOptions)
{
	if (!InOptions->CameraCalibration)
	{
		UE_LOG(LogMetaHumanRobustFeatureMatcher, Error, TEXT("Missing Camera Calibration asset"));
		return false;
	}

	if (InCaptureData->ImageSequences.Num() < 2)
	{
		UE_LOG(LogMetaHumanRobustFeatureMatcher, Error, TEXT("Feature matching process expects two cameras but found %d"), InCaptureData->ImageSequences.Num());
		return false;
	}

	if (!(IsValid(InCaptureData->ImageSequences[0]) && IsValid(InCaptureData->ImageSequences[1])))
	{
		UE_LOG(LogMetaHumanRobustFeatureMatcher, Error, TEXT("Image sequences are invalid"));
		return false;
	}

	TArray<FCameraCalibration> Calibrations;
	TArray<TPair<FString, FString>> StereoReconstructionPairs;
	InOptions->CameraCalibration->ConvertToTrackerNodeCameraModels(Calibrations, StereoReconstructionPairs);

	bool bSuccess = FeatureMatcher->Init(Calibrations, InOptions->FeatureMatchErrorThreshold, InOptions->RatioThreshold);
	if (!bSuccess)
	{
		UE_LOG(LogMetaHumanRobustFeatureMatcher, Error, TEXT("Failed to initialize the feature matching process."));
		return false;
	};

	FIntVector2 ImageDimensions(0);

	static constexpr int32 ExpectedNumberOfCameras = 2;

	for (int32 Index = 0; Index < ExpectedNumberOfCameras; ++Index)
	{
		UImgMediaSource* View = InCaptureData->ImageSequences[Index];

		int32 NumberOfImages = 0;
		FImageSequenceUtils::GetImageSequenceInfoFromAsset(View, ImageDimensions, NumberOfImages);

		FString FullPath;
		TArray<FString> FileNames;
		FImageSequenceUtils::GetImageSequencePathAndFilesFromAsset(View, FullPath, FileNames);

		TArray<FString>& ImagePath = StereoPairImagePaths.Add(View->GetName());

		for (const FString& FileName : FileNames)
		{
			ImagePath.Add(FullPath / FileName);
		}

		FeatureMatcher->AddCamera(Calibrations[Index].CameraId, ImageDimensions.X, ImageDimensions.Y);
	}

	return true;
}

bool UMetaHumanRobustFeatureMatcher::DetectFeatures(int64 InFrame)
{
	if (StereoPairImagePaths.IsEmpty())
	{
		UE_LOG(LogMetaHumanRobustFeatureMatcher, Error, TEXT("Feature matcher isn't initialized"));
		return false;
	}

	TArray<TArray64<uint8>> ArrayImageData;
	for (const TPair<FString, TArray<FString>>& CameraFrames : StereoPairImagePaths)
	{
		const TArray<FString>& CameraFramePaths = CameraFrames.Value;
		if (!CameraFrames.Value.IsValidIndex(InFrame))
		{
			UE_LOG(LogMetaHumanRobustFeatureMatcher, Error, TEXT("Camera %s doesn't contain the frame %d."), *CameraFrames.Key, InFrame);
			return false;
		}

		TArray64<uint8> CameraImage = UE::MetaHuman::Image::GetGrayscaleImageData(CameraFramePaths[InFrame]);

		if (CameraImage.IsEmpty())
		{
			UE_LOG(LogMetaHumanRobustFeatureMatcher, Error, TEXT("Image %d for camera %s couldn't be read."), InFrame, *CameraFrames.Key);
			return false;
		}

		ArrayImageData.Add(CameraImage);
	}
	
	TArray<const unsigned char*> ImageData;
	for (const TArray64<uint8>& ArrayImage : ArrayImageData)
	{
		ImageData.Add(ArrayImage.GetData());
	}

	return FeatureMatcher->DetectFeatures(InFrame, ImageData);
}

FDetectedFeatures UMetaHumanRobustFeatureMatcher::GetFeatures(int64 InFrame)
{
	TArray<FVector2D> OutPoints3d;
	TArray<TArray<FVector2D>> OutCameraPoints;
	TArray<TArray<FVector2D>> OutPoints3dReprojected;

	FDetectedFeatures DetectedFeatures;
	bool bSuccess = FeatureMatcher->GetFeatures(InFrame, OutPoints3d, OutCameraPoints, OutPoints3dReprojected);
	if (!bSuccess)
	{
		return DetectedFeatures;
	}

	DetectedFeatures.FrameIndex = InFrame;
	DetectedFeatures.Points3d = MoveTemp(OutPoints3d);

	Algo::Transform(OutCameraPoints, DetectedFeatures.CameraPoints, [](const TArray<FVector2D>& InElem)
					{
						FCameraPoints CameraPoints;
						CameraPoints.Points = InElem;
						return CameraPoints;
					});

	Algo::Transform(OutPoints3dReprojected, DetectedFeatures.Points3dReprojected, [](const TArray<FVector2D>& InElem)
					{
						FCameraPoints CameraPoints;
						CameraPoints.Points = InElem;
						return CameraPoints;
					});

	return DetectedFeatures;
}

TArray<FString> UMetaHumanRobustFeatureMatcher::GetImagePaths(const FString& InCameraName)
{
	if (TArray<FString>* Found = StereoPairImagePaths.Find(InCameraName))
	{
		return *Found;
	}

	return TArray<FString>();
}

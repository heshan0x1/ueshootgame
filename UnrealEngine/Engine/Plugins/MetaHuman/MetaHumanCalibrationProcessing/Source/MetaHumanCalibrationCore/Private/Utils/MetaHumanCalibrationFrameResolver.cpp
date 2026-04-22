// Copyright Epic Games, Inc. All Rights Reserved.

#include "Utils/MetaHumanCalibrationFrameResolver.h"
#include "Utils/MetaHumanCalibrationUtils.h"

#include "ImgMediaSource.h"
#include "ParseTakeUtils.h"

#include "FrameNumberTransformer.h"
#include "SequencedImageTrackInfo.h"

namespace UE::MetaHuman::Private
{

static FMetaHumanCalibrationFrameResolver::FCameraDescriptor GetCameraDescriptor(TNotNull<UImgMediaSource*> InMediaSource)
{
	check(IsValid(InMediaSource));

	TArray<FString> CameraPaths = Image::GetImagePaths(InMediaSource);
	FTimecode Timecode = InMediaSource->StartTimecode;
	FFrameRate FrameRate = InMediaSource->FrameRateOverride;

	return FMetaHumanCalibrationFrameResolver::FCameraDescriptor(MoveTemp(CameraPaths),
																 MoveTemp(Timecode),
																 MoveTemp(FrameRate));
}

static TMap<FString, FString> GetTimecodedFrames(const TArray<FString>& InCameraPaths,
												 const FTimecode& InTimecode,
												 const FFrameRate& InFrameRate,
												 const FFrameRate& InTargetFrameRate)
{
	TMap<FString, FString> CameraTimecodedFrame;

	FFrameNumberTransformer Transformer(InTargetFrameRate, InFrameRate);
	FFrameNumber StartingFrameNumber = InTimecode.ToFrameNumber(InFrameRate);

	for (int32 Index = 0; Index < InCameraPaths.Num(); ++Index)
	{
		int32 FrameNumber = Transformer.Transform(StartingFrameNumber.Value + Index);
		FTimecode NewTimecode = FTimecode::FromFrameNumber(FrameNumber, InTargetFrameRate);

		if (!CameraTimecodedFrame.Contains(NewTimecode.ToString()))
		{
			CameraTimecodedFrame.Add(NewTimecode.ToString(), InCameraPaths[Index]);
		}
	}

	return CameraTimecodedFrame;
}

static TArray<FMetaHumanCalibrationFramePaths> GetStereoFramePaths(const FMetaHumanCalibrationFrameResolver::FCameraDescriptor& InFirstCamera,
																   const FMetaHumanCalibrationFrameResolver::FCameraDescriptor& InSecondCamera)
{
	FFrameRate FirstFrameRate = InFirstCamera.GetFrameRate();
	FFrameRate SecondFrameRate = InSecondCamera.GetFrameRate();

	bool bFirstFrameRateValid = FirstFrameRate.IsValid() && !FMath::IsNearlyZero(FirstFrameRate.AsDecimal());
	bool bSecondFrameRateValid = SecondFrameRate.IsValid() && !FMath::IsNearlyZero(SecondFrameRate.AsDecimal());

	if (!bFirstFrameRateValid && bSecondFrameRateValid)
	{
		FirstFrameRate = SecondFrameRate;
	}
	else if (bFirstFrameRateValid && !bSecondFrameRateValid)
	{
		SecondFrameRate = FirstFrameRate;
	}
	else if (!bFirstFrameRateValid && !bSecondFrameRateValid)
	{
		FirstFrameRate = FFrameRate(60, 1);
		SecondFrameRate = FFrameRate(60, 1);
	}

	if (!FrameRatesAreCompatible(FirstFrameRate, SecondFrameRate))
	{
		return TArray<FMetaHumanCalibrationFramePaths>();
	}

	FFrameRate TargetFrameRate = 
		FirstFrameRate.AsDecimal() < SecondFrameRate.AsDecimal() ?
		SecondFrameRate : FirstFrameRate;

	TMap<FString, FString> FirstCameraTimecodedFrames = GetTimecodedFrames(InFirstCamera.GetImagePaths(),
																		   InFirstCamera.GetTimecode(),
																		   FirstFrameRate,
																		   TargetFrameRate);

	TMap<FString, FString> SecondCameraTimecodedFrames = GetTimecodedFrames(InSecondCamera.GetImagePaths(),
																			InSecondCamera.GetTimecode(),
																			SecondFrameRate,
																			TargetFrameRate);

	TArray<FMetaHumanCalibrationFramePaths> Result;
	for (const TPair<FString, FString>& FirstCameraFrame : FirstCameraTimecodedFrames)
	{
		for (const TPair<FString, FString>& SecondCameraFrame : SecondCameraTimecodedFrames)
		{
			if (FirstCameraFrame.Key == SecondCameraFrame.Key)
			{
				FMetaHumanCalibrationFramePaths StereoFramePaths;
				StereoFramePaths.FirstCamera = FirstCameraFrame.Value;
				StereoFramePaths.SecondCamera = SecondCameraFrame.Value;

				Result.Add(MoveTemp(StereoFramePaths));

				break;
			}
		}
	}

	return Result;
}

static TArray<FMetaHumanCalibrationFramePaths> GetStereoFramePaths(const UFootageCaptureData* InCaptureData)
{
	FMetaHumanCalibrationFrameResolver::FCameraDescriptor FirstCamera = 
		GetCameraDescriptor(InCaptureData->ImageSequences[0]);
	FMetaHumanCalibrationFrameResolver::FCameraDescriptor SecondCamera = 
		GetCameraDescriptor(InCaptureData->ImageSequences[1]);

	return GetStereoFramePaths(FirstCamera, SecondCamera);
}

}

FMetaHumanCalibrationFrameResolver::FCameraDescriptor::FCameraDescriptor(TArray<FString> InImagePaths, 
																		 FTimecode InTimecode, 
																		 FFrameRate InFrameRate)
	: ImagePaths(MoveTemp(InImagePaths))
	, Timecode(MoveTemp(InTimecode))
	, FrameRate(MoveTemp(InFrameRate))
{
}

const TArray<FString>& FMetaHumanCalibrationFrameResolver::FCameraDescriptor::GetImagePaths() const
{
	return ImagePaths;
}

FTimecode FMetaHumanCalibrationFrameResolver::FCameraDescriptor::GetTimecode() const
{
	return Timecode;
}

FFrameRate FMetaHumanCalibrationFrameResolver::FCameraDescriptor::GetFrameRate() const
{
	return FrameRate;
}

TOptional<FMetaHumanCalibrationFrameResolver> FMetaHumanCalibrationFrameResolver::CreateFromCaptureData(const UFootageCaptureData* InCaptureData)
{
	if (IsValid(InCaptureData) && InCaptureData->ImageSequences.Num() >= 2)
	{
		if (IsValid(InCaptureData->ImageSequences[0]) && IsValid(InCaptureData->ImageSequences[1]))
		{
			return FMetaHumanCalibrationFrameResolver(InCaptureData);
		}
	}

	return {};
}

FMetaHumanCalibrationFrameResolver::FMetaHumanCalibrationFrameResolver(const UFootageCaptureData* InCaptureData)
{
	check(InCaptureData->ImageSequences.Num() >= 2);

	CalibrationFramePaths = UE::MetaHuman::Private::GetStereoFramePaths(InCaptureData);
}

FMetaHumanCalibrationFrameResolver::FMetaHumanCalibrationFrameResolver(const FCameraDescriptor& InFirstCamera, 
																	   const FCameraDescriptor& InSecondCamera)
{
	CalibrationFramePaths = UE::MetaHuman::Private::GetStereoFramePaths(InFirstCamera, InSecondCamera);
}

bool FMetaHumanCalibrationFrameResolver::GetFramePathsForCameraIndex(int32 InCameraIndex, TArray<FString>& OutFramePaths) const
{
	Algo::Transform(CalibrationFramePaths, OutFramePaths, [InCameraIndex](const FMetaHumanCalibrationFramePaths& InStereoFramePaths) -> FString
	{
		if (InCameraIndex == 0)
		{
			return InStereoFramePaths.FirstCamera;
		}
		else
		{
			return InStereoFramePaths.SecondCamera;
		}
	});

	if (OutFramePaths.IsEmpty())
	{
		return false;
	}

	return true;
}

bool FMetaHumanCalibrationFrameResolver::GetCalibrationFramePathsForFrameIndex(int32 InFrameIndex, FMetaHumanCalibrationFramePaths& OutCalibrationFrame) const
{
	if (!CalibrationFramePaths.IsValidIndex(InFrameIndex))
	{
		return false;
	}

	OutCalibrationFrame = CalibrationFramePaths[InFrameIndex];
	return true;
}

TArray<FMetaHumanCalibrationFramePaths> FMetaHumanCalibrationFrameResolver::GetCalibrationFramePaths() const
{
	return CalibrationFramePaths;
}

bool FMetaHumanCalibrationFrameResolver::HasFrames() const
{
	return !CalibrationFramePaths.IsEmpty();
}

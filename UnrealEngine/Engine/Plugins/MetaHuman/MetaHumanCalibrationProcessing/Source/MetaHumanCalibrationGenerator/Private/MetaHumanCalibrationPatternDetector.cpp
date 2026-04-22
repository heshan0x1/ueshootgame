// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanCalibrationPatternDetector.h"

#include "Async/ParallelFor.h"
#include "Async/Monitor.h"
#include "Async/TaskProgress.h"

#include "Misc/FileHelper.h"

#include "Utils/MetaHumanCalibrationUtils.h"

TUniquePtr<FMetaHumanCalibrationPatternDetector> 
FMetaHumanCalibrationPatternDetector::CreateFromExistingCalibrator(TSharedPtr<UE::Wrappers::FMetaHumanStereoCalibrator> InStereoCalibrator)
{
	return MakeUnique<FMetaHumanCalibrationPatternDetector>(FPrivateToken(), MoveTemp(InStereoCalibrator));
}

TUniquePtr<FMetaHumanCalibrationPatternDetector> 
FMetaHumanCalibrationPatternDetector::CreateFromNew(const FPatternInfo& InBoardInfo, const TMap<FString, FIntVector2>& InCameras, float InScale)
{
	TSharedPtr<UE::Wrappers::FMetaHumanStereoCalibrator> StereoCalibrator = 
		MakeShared<UE::Wrappers::FMetaHumanStereoCalibrator>();

	bool bResult = StereoCalibrator->Init(InBoardInfo.Width, InBoardInfo.Height, InBoardInfo.SquareSize);
	if (!bResult)
	{
		return nullptr;
	}

	for (const TPair<FString, FIntVector2>& Camera : InCameras)
	{
		bResult &= StereoCalibrator->AddCamera(Camera.Key, Camera.Value.X * InScale, Camera.Value.Y * InScale);
	}

	if (!bResult)
	{
		return nullptr;
	}

	return MakeUnique<FMetaHumanCalibrationPatternDetector>(FPrivateToken(), MoveTemp(StereoCalibrator), InScale);
}

FMetaHumanCalibrationPatternDetector::FMetaHumanCalibrationPatternDetector(
	FPrivateToken,
	TSharedPtr<UE::Wrappers::FMetaHumanStereoCalibrator> InStereoCalibrator,
	TOptional<float> InScale)
	: StereoCalibrator(MoveTemp(InStereoCalibrator))
	, Scale(MoveTemp(InScale))
{
}

void FMetaHumanCalibrationPatternDetector::SetOnProgressReporter(FProgressReporter InProgressReporter)
{
	ProgressReporter = MoveTemp(InProgressReporter);
}

FMetaHumanCalibrationPatternDetector::FDetectedFrames 
FMetaHumanCalibrationPatternDetector::DetectPatterns(const TPair<FString, FString>& InCameraNames,
													 const TPair<TArray<FString>, TArray<FString>>& InFilteredFrames,
													 float InSharpnessThreshold,
													 FOnFailureFrameProvider InOnFailureFrameProvider)
{
	using namespace UE::CaptureManager;
	using namespace UE::Wrappers;

	TMonitor<FDetectedFrames> ValidFrames;

	const FString& FirstCameraName = InCameraNames.Key;
	const FString& SecondCameraName = InCameraNames.Value;

	static constexpr int32 NumberOfThreads = 4;
	const int32 TotalNumberOfImages = InFilteredFrames.Key.Num();
	int32 NumberOfThreadsActuallyUsed = NumberOfThreads > TotalNumberOfImages ? TotalNumberOfImages : NumberOfThreads;

	const int32 NumberOfImagesPerThread = InFilteredFrames.Key.Num() / NumberOfThreadsActuallyUsed;

	TSharedPtr<FTaskProgress> TaskProgress;
	if (ProgressReporter.IsBound())
	{
		TaskProgress = 
			MakeShared<FTaskProgress>(NumberOfThreadsActuallyUsed, FTaskProgress::FProgressReporter::CreateLambda([this](double InProgress)
			{
				ProgressReporter.Execute(InProgress);
			}));
	}

	ParallelFor(NumberOfThreadsActuallyUsed, [&](int32 InChunkIndex)
	{
		FTaskProgress::FTask Task = TaskProgress ? TaskProgress->StartTask() : FTaskProgress::FTask();
		int32 StartIndex = InChunkIndex * NumberOfImagesPerThread;

		if (StartIndex > TotalNumberOfImages)
		{
			return;
		}

		int32 NumberOfFrames = (StartIndex + NumberOfImagesPerThread) <= TotalNumberOfImages ?
			NumberOfImagesPerThread : FMath::Min(TotalNumberOfImages - NumberOfImagesPerThread, 0);

		const int32 Left = TotalNumberOfImages - (StartIndex + NumberOfImagesPerThread);
		if (Left < NumberOfImagesPerThread)
		{
			NumberOfFrames += Left;
		}

		for (int32 Index = StartIndex; Index < StartIndex + NumberOfFrames; ++Index)
		{
			FFramePaths CurrentFrame = { InFilteredFrames.Key[Index], InFilteredFrames.Value[Index] };
			TOptional<FDetectedFrame> DetectedFrame =
				DetectPattern({ FirstCameraName, SecondCameraName }, CurrentFrame, InSharpnessThreshold);

			if (!DetectedFrame.IsSet() && InOnFailureFrameProvider.IsBound())
			{
				for (int32 Try = 1; Try <= 5; ++Try)
				{
					TOptional<FFramePaths> NewFrame = InOnFailureFrameProvider.Execute(CurrentFrame, Try);

					if (!NewFrame.IsSet())
					{
						break;
					}

					DetectedFrame =
						DetectPattern({ FirstCameraName, SecondCameraName }, NewFrame.GetValue(), InSharpnessThreshold);

					if (DetectedFrame.IsSet())
					{
						break;
					}
				}
			}

			Task.Update(1.0 / (StartIndex + NumberOfFrames));

			if (DetectedFrame.IsSet())
			{
				TMonitor<FDetectedFrames>::FHelper Helper = ValidFrames.Lock();
				Helper->Emplace(MoveTemp(DetectedFrame.GetValue()));
			}
		}
	});

	return ValidFrames.Claim();
}

TOptional<FMetaHumanCalibrationPatternDetector::FDetectedFrame> 
FMetaHumanCalibrationPatternDetector::DetectPattern(const TPair<FString, FString>& InCameraNames,
													const TPair<FString, FString>& InFramePath,
													float InSharpnessThreshold)
{
	using namespace UE::Wrappers;
	using namespace UE::MetaHuman;

	FString FirstCameraImagePath = InFramePath.Key;
	TOptional<FImage> FirstCameraImageOpt = Image::GetGrayscaleImage(FirstCameraImagePath);
	if (!FirstCameraImageOpt.IsSet())
	{
		return {};
	}

	FString SecondCameraImagePath = InFramePath.Value;
	TOptional<FImage> SecondCameraImageOpt = Image::GetGrayscaleImage(SecondCameraImagePath);
	if (!SecondCameraImageOpt.IsSet())
	{
		return {};
	}

	FImage FirstCameraImage = MoveTemp(FirstCameraImageOpt.GetValue());
	FImage SecondCameraImage = MoveTemp(SecondCameraImageOpt.GetValue());

	float SharpnessThreshold = InSharpnessThreshold;

	if (Scale.IsSet())
	{
		float ScaleValue = Scale.GetValue();

		FImageCore::ResizeImageInPlace(FirstCameraImage, FirstCameraImage.SizeX * ScaleValue, FirstCameraImage.SizeY * ScaleValue);
		FImageCore::ResizeImageInPlace(SecondCameraImage, SecondCameraImage.SizeX * ScaleValue, SecondCameraImage.SizeY * ScaleValue);

		SharpnessThreshold *= ScaleValue;
	}

	TArray<FVector2D> FirstCameraImageCornerPoints;
	double FirstCameraImageChessboardSharpness;
	bool bFirstCameraDetectResult = StereoCalibrator->DetectPattern(InCameraNames.Key, FirstCameraImage.RawData.GetData(), FirstCameraImageCornerPoints, FirstCameraImageChessboardSharpness);

	TArray<FVector2D> SecondCameraImageCornerPoints;
	double SecondCameraImageChessboardSharpness;
	bool bSecondCameraDetectResult = StereoCalibrator->DetectPattern(InCameraNames.Value, SecondCameraImage.RawData.GetData(), SecondCameraImageCornerPoints, SecondCameraImageChessboardSharpness);

	if (bFirstCameraDetectResult &&
		bSecondCameraDetectResult &&
		FirstCameraImageChessboardSharpness < SharpnessThreshold &&
		SecondCameraImageChessboardSharpness < SharpnessThreshold)
	{
		if (Scale.IsSet())
		{
			Points::ScalePointsInPlace(FirstCameraImageCornerPoints, Scale.GetValue());
			Points::ScalePointsInPlace(SecondCameraImageCornerPoints, Scale.GetValue());
		}

		FDetectedFrame ValidFrame;
		ValidFrame.Emplace(InCameraNames.Key, FirstCameraImageCornerPoints);
		ValidFrame.Emplace(InCameraNames.Value, SecondCameraImageCornerPoints);

		return ValidFrame;
	}

	return {};
}

// Copyright Epic Games, Inc. All Rights Reserved.

#include "Utils/MetaHumanCalibrationUtils.h"

#include "Misc/FileHelper.h"

#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Modules/ModuleManager.h"

#include "ImageSequenceUtils.h"
#include "ImgMediaSource.h"

#include "Async/ParallelFor.h"

namespace UE::MetaHuman
{
namespace Image
{

namespace Private
{

static FImage ConvertBGRAToGrayscale(const FImageView& InSource)
{
	FImage Dest;
	Dest.Init(InSource.SizeX, InSource.SizeY, ERawImageFormat::G8, InSource.GammaSpace);

	if (InSource.Format == ERawImageFormat::BGRA8)
	{
		const int64 NumTexels = InSource.GetNumPixels();
		int64 TexelsPerJob;
		int32 NumJobs = ImageParallelForComputeNumJobsForPixels(TexelsPerJob, NumTexels);

		const FColor* SrcColors = (const FColor*) InSource.RawData;
		uint8* DestLum = Dest.RawData.GetData();

		ParallelFor(TEXT("ConvertBGRAToGrayscale"), NumJobs, 1, [=](int64 JobIndex)
		{
			const int64 StartIndex = JobIndex * TexelsPerJob;
			const int64 EndIndex = FMath::Min(StartIndex + TexelsPerJob, NumTexels);

			for (int64 TexelIndex = StartIndex; TexelIndex < EndIndex; ++TexelIndex)
			{
				double Gray = 0.114 * SrcColors[TexelIndex].B + 0.587 * SrcColors[TexelIndex].G + 0.299 * SrcColors[TexelIndex].R;
				DestLum[TexelIndex] = FMath::RoundToInt32(Gray);

			}
		}, EParallelForFlags::Unbalanced);

		return Dest;
	}

	return Dest;
}

}

TArray<FString> GetImagePaths(TObjectPtr<UImgMediaSource> InImgMediaSource)
{
	FString ImageDirectoryPath;
	TArray<FString> ImageNames;
	TArray<FString> ImagePaths;
	FImageSequenceUtils::GetImageSequencePathAndFilesFromAsset(InImgMediaSource, ImageDirectoryPath, ImageNames);

	for (FString ImageName : ImageNames)
	{
		ImagePaths.Add(FPaths::Combine(ImageDirectoryPath, ImageName));
	}

	return ImagePaths;
}

FString GetStringFromTimespan(const FTimespan& InTimespan)
{
	FString Result;
	if (InTimespan.GetHours() == 0)
	{
		Result = InTimespan.ToString(TEXT("%m:%s.%f"));
	}
	else
	{
		Result = InTimespan.ToString();
	}

	// Deleting + or - at the beginning of the string
	Result.RemoveAt(0, 1);

	return Result;
}

TOptional<FImage> GetGrayscaleImage(const FString& InFullImagePath)
{
	IImageWrapperModule& ImageWrapperModule =
		FModuleManager::Get().LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));

	EImageFormat ImageFormat = ImageWrapperModule.GetImageFormatFromExtension(*InFullImagePath);
	if (ImageFormat == EImageFormat::Invalid)
	{
		return {};
	}

	TArray<uint8> RawFileData;
	if (!FFileHelper::LoadFileToArray(RawFileData, *InFullImagePath))
	{
		return {};
	}

	TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(ImageFormat);

	FImage Image;
	if (!ImageWrapperModule.DecompressImage(RawFileData.GetData(), RawFileData.Num(), Image))
	{
		return {};
	}

	FImage Grayscale;
	if (Image.Format == ERawImageFormat::BGRA8)
	{
		Grayscale = Private::ConvertBGRAToGrayscale(Image);
	}
	else
	{
		Image.CopyTo(Grayscale, ERawImageFormat::G8, Image.GammaSpace);
	}
	
	return Grayscale;
}

TArray64<uint8> GetGrayscaleImageData(const FString& InFullImagePath)
{
	IImageWrapperModule& ImageWrapperModule =
		FModuleManager::Get().LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));

	EImageFormat ImageFormat = ImageWrapperModule.GetImageFormatFromExtension(*InFullImagePath);
	if (ImageFormat == EImageFormat::Invalid)
	{
		return TArray64<uint8>();
	}

	TArray<uint8> RawFileData;
	if (!FFileHelper::LoadFileToArray(RawFileData, *InFullImagePath))
	{
		return TArray64<uint8>();
	}

	TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(ImageFormat);
	ImageWrapper->SetCompressed(RawFileData.GetData(), RawFileData.Num());

	TArray64<uint8> ImageData;
	static constexpr int32 BitDepth = 8;
	ImageWrapper->GetRaw(ERGBFormat::Gray, BitDepth, ImageData);

	if (ImageData.IsEmpty())
	{
		return TArray64<uint8>();
	}

	return ImageData;
}

TPair<TArray<FString>, TArray<FString>> FilterFramePaths(const UFootageCaptureData* InCaptureData, FFilteringPredicate InPredicate)
{
	check(InPredicate.IsBound());

	FString FirstCameraName = InCaptureData->ImageSequences[0]->GetName();
	FString SecondCameraName = InCaptureData->ImageSequences[1]->GetName();

	TArray<FString> FirstCameraImageNames;
	TArray<FString> SecondCameraImageNames;
	FImageSequenceUtils::GetImageSequenceFilesFromPath(InCaptureData->ImageSequences[0]->GetFullPath(), FirstCameraImageNames);
	FImageSequenceUtils::GetImageSequenceFilesFromPath(InCaptureData->ImageSequences[1]->GetFullPath(), SecondCameraImageNames);

	check(FirstCameraImageNames.Num() == SecondCameraImageNames.Num());

	TArray<FString> FirstCameraImagePaths;
	TArray<FString> SecondCameraImagePaths;
	for (int32 FrameIndex = 0; FrameIndex < FirstCameraImageNames.Num(); ++FrameIndex)
	{
		if (InPredicate.Execute(FrameIndex))
		{
			FString FirstCameraImagePath = FPaths::Combine(InCaptureData->ImageSequences[0]->GetFullPath(), FirstCameraImageNames[FrameIndex]);
			FString SecondCameraImagePath = FPaths::Combine(InCaptureData->ImageSequences[1]->GetFullPath(), SecondCameraImageNames[FrameIndex]);

			FirstCameraImagePaths.Add(MoveTemp(FirstCameraImagePath));
			SecondCameraImagePaths.Add(MoveTemp(SecondCameraImagePath));
		}
	}

	TPair<TArray<FString>, TArray<FString>> Output;
	Output.Key = MoveTemp(FirstCameraImagePaths);
	Output.Value = MoveTemp(SecondCameraImagePaths);

	check(Output.Key.Num() == Output.Value.Num());

	return Output;
}

TPair<TArray<FString>, TArray<FString>> FilterFramePaths(const TPair<TArray<FString>, TArray<FString>>& InImagePaths,
														 FFilteringPredicate InPredicate)
{
	const auto& [FirstCameraImagePaths, SecondCameraImagePaths] = InImagePaths;

	TArray<FString> FilteredFirstCameraPaths;
	TArray<FString> FilteredSecondCameraPaths;
	for (int32 FrameIndex = 0; FrameIndex < FirstCameraImagePaths.Num(); ++FrameIndex)
	{
		if (InPredicate.Execute(FrameIndex))
		{
			FilteredFirstCameraPaths.Add(FirstCameraImagePaths[FrameIndex]);
			FilteredSecondCameraPaths.Add(SecondCameraImagePaths[FrameIndex]);
		}
	}

	TPair<TArray<FString>, TArray<FString>> Output;
	Output.Key = MoveTemp(FilteredFirstCameraPaths);
	Output.Value = MoveTemp(FilteredSecondCameraPaths);

	check(Output.Key.Num() == Output.Value.Num());

	return Output;
}

TArray<int32> FilterFrameIndices(const TPair<TArray<FString>, TArray<FString>>& InImagePaths,
								 FFilteringPredicate InPredicate)
{
	const auto& [FirstCameraImagePaths, SecondCameraImagePaths] = InImagePaths;

	check(FirstCameraImagePaths.Num() == SecondCameraImagePaths.Num());

	TArray<int32> FrameIndices;
	for (int32 FrameIndex = 0; FrameIndex < FirstCameraImagePaths.Num(); ++FrameIndex)
	{
		if (InPredicate.Execute(FrameIndex))
		{
			FrameIndices.Add(FrameIndex);
		}
	}

	return FrameIndices;
}

}

namespace Points {

void ScalePointsInPlace(TArray<FVector2D>& InPoints, float InScale)
{
	for (FVector2D& Point : InPoints)
	{
		Point = Point / InScale;
	}
}

FVector2D MapTexturePointToLocalWidgetSpace(const FVector2D& InPoint,
											const FVector2D& InTextureSize,
											const FBox2D& InUV,
											const FVector2D& InWidgetSize)
{
	FVector2D UVMin = InUV.Min;
	FVector2D UVSize = InUV.GetSize();

	FVector2D NormalizedUV = InPoint / InTextureSize;
	FVector2D RelativeUV = (NormalizedUV - UVMin) / UVSize;

	return RelativeUV * InWidgetSize;
}

FVector2D MapWidgetPointToTextureSpace(const FVector2D& InWidgetPoint,
									   const FVector2D& InWidgetSize,
									   const FBox2D& InUV,
									   const FVector2D& InTextureSize)
{
	FVector2D UVMin = InUV.Min;
	FVector2D UVSize = InUV.GetSize();

	FVector2D NormalizedUV = InWidgetPoint / InWidgetSize;
	FVector2D RelativeUV = (NormalizedUV * UVSize) + UVMin;

	return RelativeUV * InTextureSize;
}

FVector2D MapTextureSizeToLocalWidgetSize(const FVector2D& InBeginPoint,
										  const FVector2D& InCurrentSize,
										  const FVector2D& InTextureSize,
										  const FBox2D& InUV,
										  const FVector2D& InWidgetSize)
{
	FVector2D BeginPoint = MapTexturePointToLocalWidgetSpace(InBeginPoint, InTextureSize, InUV, InWidgetSize);
	FVector2D EndPoint = MapTexturePointToLocalWidgetSpace(InBeginPoint + InCurrentSize, InTextureSize, InUV, InWidgetSize);

	BeginPoint = FVector2D::Clamp(BeginPoint, FVector2D(0.0, 0.0), InWidgetSize);
	EndPoint = FVector2D::Clamp(EndPoint, FVector2D(0.0, 0.0), InWidgetSize);

	return EndPoint - BeginPoint;
}

FVector2D MapRealTextureSizeToLocalWidgetSize(const FVector2D& InBeginPoint,
											  const FVector2D& InCurrentSize,
											  const FVector2D& InTextureSize,
											  const FBox2D& InUV,
											  const FVector2D& InWidgetSize)
{
	FVector2D BeginPoint = MapTexturePointToLocalWidgetSpace(InBeginPoint, InTextureSize, InUV, InWidgetSize);
	FVector2D EndPoint = MapTexturePointToLocalWidgetSpace(InBeginPoint + InCurrentSize, InTextureSize, InUV, InWidgetSize);

	return EndPoint - BeginPoint;
}

bool IsOutsideWidgetBounds(const FVector2D& ScaledPoint, const FVector2D& WidgetSize)
{
	return ScaledPoint.X < 0.0f || ScaledPoint.Y < 0.0f ||
		ScaledPoint.X > WidgetSize.X || ScaledPoint.Y > WidgetSize.Y;
}
}

}
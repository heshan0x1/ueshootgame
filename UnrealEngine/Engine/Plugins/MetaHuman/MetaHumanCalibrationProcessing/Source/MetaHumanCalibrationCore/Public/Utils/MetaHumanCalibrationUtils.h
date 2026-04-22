// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/Tuple.h"
#include "Containers/UnrealString.h"
#include "CaptureData.h"

#include "ImageCore.h"

#define UE_API METAHUMANCALIBRATIONCORE_API

namespace UE::MetaHuman
{

namespace Image
{

DECLARE_DELEGATE_RetVal_OneParam(bool, FFilteringPredicate, int32);

UE_API TArray<FString> GetImagePaths(TObjectPtr<UImgMediaSource> InImgMediaSource);
UE_API FString GetStringFromTimespan(const FTimespan& InTimespan);
UE_API TOptional<FImage> GetGrayscaleImage(const FString& InFullImagePath);
UE_API TArray64<uint8> GetGrayscaleImageData(const FString& InFullImagePath);

UE_API TPair<TArray<FString>, TArray<FString>> FilterFramePaths(const UFootageCaptureData* InCaptureData, FFilteringPredicate InPredicate);
UE_API TPair<TArray<FString>, TArray<FString>> FilterFramePaths(const TPair<TArray<FString>, TArray<FString>>& InImagePaths,
																FFilteringPredicate InPredicate);
UE_API TArray<int32> FilterFrameIndices(const TPair<TArray<FString>, TArray<FString>>& InImagePaths,
										FFilteringPredicate InPredicate);

template<typename Predicate>
TPair<TArray<FString>, TArray<FString>> FilterFramePaths(const UFootageCaptureData* InCaptureData, Predicate&& InPredicate)
{
	return FilterFramePaths(InCaptureData, FFilteringPredicate::CreateLambda(MoveTemp(InPredicate)));
}

template<typename Predicate>
TPair<TArray<FString>, TArray<FString>> FilterFramePaths(const TPair<TArray<FString>, TArray<FString>>& InImagePaths,
														 Predicate&& InPredicate)
{
	return FilterFramePaths(InImagePaths, FFilteringPredicate::CreateLambda(MoveTemp(InPredicate)));
}

template<typename Predicate>
TArray<int32> FilterFrameIndices(const TPair<TArray<FString>, TArray<FString>>& InImagePaths,
								 Predicate&& InPredicate)
{
	return FilterFrameIndices(InImagePaths, FFilteringPredicate::CreateLambda(MoveTemp(InPredicate)));
}

}

namespace Points 
{

UE_API void ScalePointsInPlace(TArray<FVector2D>& InPoints, float InScale);

UE_API FVector2D MapTexturePointToLocalWidgetSpace(const FVector2D& InPoint,
												   const FVector2D& InTextureSize,
												   const FBox2D& InUV,
												   const FVector2D& InWidgetSize);

UE_API FVector2D MapWidgetPointToTextureSpace(const FVector2D& InWidgetPoint,
											  const FVector2D& InWidgetSize,
											  const FBox2D& InUV,
											  const FVector2D& InTextureSize);

UE_API FVector2D MapTextureSizeToLocalWidgetSize(const FVector2D& InBeginPoint,
												 const FVector2D& InCurrentSize,
												 const FVector2D& InTextureSize,
												 const FBox2D& InUV,
												 const FVector2D& InWidgetSize);

UE_API FVector2D MapRealTextureSizeToLocalWidgetSize(const FVector2D& InBeginPoint,
													 const FVector2D& InCurrentSize,
													 const FVector2D& InTextureSize,
													 const FBox2D& InUV,
													 const FVector2D& InWidgetSize);

UE_API bool IsOutsideWidgetBounds(const FVector2D& ScaledPoint, const FVector2D& WidgetSize);
}

}

#undef UE_API
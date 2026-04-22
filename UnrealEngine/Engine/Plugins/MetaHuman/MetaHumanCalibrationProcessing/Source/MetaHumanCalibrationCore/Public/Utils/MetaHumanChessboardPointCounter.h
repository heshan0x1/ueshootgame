// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Array.h"
#include "Containers/Map.h"

#include "Math/Box2D.h"
#include "Math/IntVector.h"

#define UE_API METAHUMANCALIBRATIONCORE_API

class FMetaHumanChessboardPointCounter
{
public:

	using FPoints = TArray<FVector2D>;
	using FFramePointsMap = TMap<int32, FPoints>;
	using FFramePerCameraPoints = TMap<FString, FPoints>;
	using FFramePerCameraPointsArray = TArray<FFramePerCameraPoints>;
	using FFramePerCameraPointsMap = TMap<int32, FFramePerCameraPoints>;

	UE_API FMetaHumanChessboardPointCounter(const FVector2D& InCoverageMapSize,
											const TPair<FString, FString>& InCameraNames,
											const TPair<FIntVector2, FIntVector2>& InImageSizes);

	UE_API void Invalidate(TOptional<FVector2D> InCoverageMapSize = TOptional<FVector2D>());
	UE_API void Invalidate(const FFramePerCameraPointsArray& InFrameArray, TOptional<FVector2D> InCoverageMapSize = TOptional<FVector2D>());

	UE_API void Update(const FString& InCameraName, const FPoints& InFramePoints);
	UE_API void Update(const FString& InCameraName, const FFramePointsMap& InFramePointsMap);
	UE_API void Update(const FFramePerCameraPoints& InFramePerCameraPoints);
	UE_API void Update(const FFramePerCameraPointsArray& InFramePerCameraArray);
	UE_API void Update(const FFramePerCameraPointsMap& InFramePerCameraMap);

	UE_API TArray<int32> GetBlockIndices(const FString& InCameraName);
	UE_API TArray<int32> GetOccupiedBlockIndices(const FString& InCameraName, int32 InOccupancyThreshold = 0);
	UE_API TMap<int32, int32> GetOccupiedBlockIndicesAndCount(const FString& InCameraName, int32 InOccupancyThreshold = 0);

	UE_API TOptional<int32> GetCountForBlock(const FString& InCameraName, int32 InBlockIndex) const;
	UE_API FVector2D GetBlockSize() const;
	UE_API FBox2D GetBlock(const FString& InCameraName, int32 InBlockIndex);

private:

	void InvalidateForCamera(const FString& InCamera, const FIntVector2& InImageSize);

	FVector2D CoverageMapSize;
	TPair<FString, FString> CameraNames;
	TPair<FIntVector2, FIntVector2> ImageSizes;

	TMap<FString, TArray<FBox2D>> ArrayOfBlocks;
	TMap<FString, TArray<int32>> NumberOfPointsPerBlock;
};

#undef UE_API
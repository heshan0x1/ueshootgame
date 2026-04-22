// Copyright Epic Games, Inc. All Rights Reserved.

#include "Utils/MetaHumanChessboardPointCounter.h"

FMetaHumanChessboardPointCounter::FMetaHumanChessboardPointCounter(const FVector2D& InCoverageMapSize,
																   const TPair<FString, FString>& InCameraNames,
																   const TPair<FIntVector2, FIntVector2>& InImageSizes)
	: CoverageMapSize(InCoverageMapSize)
	, CameraNames(InCameraNames)
	, ImageSizes(InImageSizes)
	
{
	Invalidate();
}

void FMetaHumanChessboardPointCounter::Invalidate(TOptional<FVector2D> InCoverageMapSize)
{
	if (InCoverageMapSize.IsSet())
	{
		CoverageMapSize = InCoverageMapSize.GetValue();
	}

	ArrayOfBlocks.Empty();
	NumberOfPointsPerBlock.Empty();

	InvalidateForCamera(CameraNames.Key, ImageSizes.Key);
	InvalidateForCamera(CameraNames.Value, ImageSizes.Value);
}

void FMetaHumanChessboardPointCounter::Invalidate(const FFramePerCameraPointsArray& InFrameArray, TOptional<FVector2D> InCoverageMapSize)
{
	Invalidate(MoveTemp(InCoverageMapSize));
	Update(InFrameArray);
}

void FMetaHumanChessboardPointCounter::Update(const FString& InCameraName, const FPoints& InPoints)
{
	check(ArrayOfBlocks.Contains(InCameraName));

	const TArray<FBox2D>& ArrayOfBlocksForCamera = ArrayOfBlocks[InCameraName];

	for (const FVector2D& Point : InPoints)
	{
		for (int32 Index = 0; Index < ArrayOfBlocksForCamera.Num(); ++Index)
		{
			if (ArrayOfBlocksForCamera[Index].IsInside(Point))
			{
				++NumberOfPointsPerBlock[InCameraName][Index];
				break;
			}
		}
	}
}

void FMetaHumanChessboardPointCounter::Update(const FString& InCameraName, const FFramePointsMap& InFramePointsMap)
{
	for (const TPair<int32, FPoints>& FramePointsPair : InFramePointsMap)
	{
		Update(InCameraName, FramePointsPair.Value);
	}
}

void FMetaHumanChessboardPointCounter::Update(const FFramePerCameraPoints& InFramePerCameraPoints)
{
	for (const TPair<FString, FPoints>& FramePerCameraPointsPair : InFramePerCameraPoints)
	{
		const TArray<FBox2D>& ArrayOfBlocksForCamera = ArrayOfBlocks[FramePerCameraPointsPair.Key];

		for (const FVector2D& Point : FramePerCameraPointsPair.Value)
		{
			for (int32 Index = 0; Index < ArrayOfBlocksForCamera.Num(); ++Index)
			{
				if (ArrayOfBlocksForCamera[Index].IsInside(Point))
				{
					++NumberOfPointsPerBlock[FramePerCameraPointsPair.Key][Index];
					break;
				}
			}
		}
	}
}

void FMetaHumanChessboardPointCounter::Update(const FFramePerCameraPointsArray& InFramePerCameraArray)
{
	for (const FFramePerCameraPoints& FramePerCameraPoints : InFramePerCameraArray)
	{
		Update(FramePerCameraPoints);
	}
}

void FMetaHumanChessboardPointCounter::Update(const FFramePerCameraPointsMap& InFramePerCameraMap)
{
	for (const TPair<int32, FFramePerCameraPoints>& FramePerCameraPointsPair : InFramePerCameraMap)
	{
		Update(FramePerCameraPointsPair.Value);
	}
}

TArray<int32> FMetaHumanChessboardPointCounter::GetBlockIndices(const FString& InCameraName)
{
	check(ArrayOfBlocks.Contains(InCameraName));

	const TArray<FBox2D>& ArrayOfBlocksForCamera = ArrayOfBlocks[InCameraName];

	TArray<int32> BlockIndices;
	for (int32 Index = 0; Index < ArrayOfBlocksForCamera.Num(); ++Index)
	{
		BlockIndices.Add(Index);
	}

	return BlockIndices;
}

TArray<int32> FMetaHumanChessboardPointCounter::GetOccupiedBlockIndices(const FString& InCameraName, int32 InOccupancyThreshold)
{
	check(ArrayOfBlocks.Contains(InCameraName));

	const TArray<FBox2D>& ArrayOfBlocksForCamera = ArrayOfBlocks[InCameraName];

	TArray<int32> BlockIndices;
	for (int32 Index = 0; Index < ArrayOfBlocksForCamera.Num(); ++Index)
	{
		TOptional<int32> CountForBlock = GetCountForBlock(InCameraName, Index);
		if (CountForBlock.IsSet() && CountForBlock.GetValue() > InOccupancyThreshold)
		{
			BlockIndices.Add(Index);
		}
	}

	return BlockIndices;
}

TMap<int32, int32> FMetaHumanChessboardPointCounter::GetOccupiedBlockIndicesAndCount(const FString& InCameraName, int32 InOccupancyThreshold)
{
	check(ArrayOfBlocks.Contains(InCameraName));

	const TArray<FBox2D>& ArrayOfBlocksForCamera = ArrayOfBlocks[InCameraName];

	TMap<int32, int32> BlockIndicesAndCount;
	for (int32 Index = 0; Index < ArrayOfBlocksForCamera.Num(); ++Index)
	{
		TOptional<int32> CountForBlock = GetCountForBlock(InCameraName, Index);
		if (CountForBlock.IsSet() && CountForBlock.GetValue() > InOccupancyThreshold)
		{
			BlockIndicesAndCount.Add(Index, CountForBlock.GetValue());
		}
	}

	return BlockIndicesAndCount;
}

TOptional<int32> FMetaHumanChessboardPointCounter::GetCountForBlock(const FString& InCameraName, int32 InBlockIndex) const
{
	if (NumberOfPointsPerBlock[InCameraName].IsValidIndex(InBlockIndex))
	{
		return NumberOfPointsPerBlock[InCameraName][InBlockIndex];
	}

	return {};
}

FVector2D FMetaHumanChessboardPointCounter::GetBlockSize() const
{
	return FVector2D(ImageSizes.Key) / CoverageMapSize;
}

FBox2D FMetaHumanChessboardPointCounter::GetBlock(const FString& InCameraName, int32 InBlockIndex)
{
	check(ArrayOfBlocks.Contains(InCameraName));
	check(ArrayOfBlocks[InCameraName].IsValidIndex(InBlockIndex));

	return ArrayOfBlocks[InCameraName][InBlockIndex];
}

void FMetaHumanChessboardPointCounter::InvalidateForCamera(const FString& InCamera, const FIntVector2& InImageSize)
{
	int32 XSize = InImageSize.X / CoverageMapSize.X;
	int32 YSize = InImageSize.Y / CoverageMapSize.Y;

	TArray<FBox2D>& CameraBlocks = ArrayOfBlocks.Add(InCamera);

	for (int32 YIndex = 0; YIndex < CoverageMapSize.Y; ++YIndex)
	{
		for (int32 XIndex = 0; XIndex < CoverageMapSize.X; ++XIndex)
		{
			FBox2D Block(FVector2D(XIndex * XSize, YIndex * YSize), FVector2D((XIndex + 1) * XSize, (YIndex + 1) * YSize));
			CameraBlocks.Add(MoveTemp(Block));
		}
	}

	NumberOfPointsPerBlock.Add(InCamera).AddZeroed(CameraBlocks.Num());
}

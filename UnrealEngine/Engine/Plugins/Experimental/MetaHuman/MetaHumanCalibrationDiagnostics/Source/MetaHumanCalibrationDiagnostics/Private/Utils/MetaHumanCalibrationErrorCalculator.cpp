// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanCalibrationErrorCalculator.h"

FMetaHumanCalibrationErrorCalculator::FMetaHumanCalibrationErrorCalculator(const FVector2D& InCoverageMapSize,
																		   const TArray<FString>& InCameraNames,
																		   const TArray<FIntVector2>& InImageSizes)
	: CoverageMapSize(InCoverageMapSize)
	, CameraNames(InCameraNames)
	, ImageSizes(InImageSizes)
{
	check(!InCameraNames.IsEmpty() && !InImageSizes.IsEmpty());
	check(InCameraNames.Num() == InImageSizes.Num());
	
	for (const FIntVector2& ImageSize : ImageSizes)
	{
		AreaOfInterest.Add(FBox2D(FVector2D::ZeroVector, FVector2D(ImageSize)));
	}

	Invalidate();
}

void FMetaHumanCalibrationErrorCalculator::SetAreaOfInterestForCamera(const FString& InCameraName, const FBox2D& InAreaOfInterest)
{
	int32 CameraIndex = CameraNames.IndexOfByKey(InCameraName);
	check(CameraIndex != INDEX_NONE);

	AreaOfInterest[CameraIndex] = InAreaOfInterest;

	InvalidateForCamera(InCameraName);
}

bool FMetaHumanCalibrationErrorCalculator::ContainsErrors() const
{
	if (ErrorsPerCameraPerFramePerBlock.IsEmpty())
	{
		return false;
	}

	return ErrorsPerCameraPerFramePerBlock.CreateConstIterator()->Value.Num() != 0;
}

bool FMetaHumanCalibrationErrorCalculator::ContainsErrors(int32 InFrame) const
{
	if (ErrorsPerCameraPerFramePerBlock.IsEmpty())
	{
		return false;
	}

	return ErrorsPerCameraPerFramePerBlock.CreateConstIterator()->Value.Contains(InFrame);
}

void FMetaHumanCalibrationErrorCalculator::Invalidate(TOptional<FVector2D> InCoverageMapSize)
{
	if (InCoverageMapSize.IsSet())
	{
		CoverageMapSize = InCoverageMapSize.GetValue();
	}

	ArrayOfBlocks.Empty();
	ErrorsPerCameraPerFramePerBlock.Empty();

	Reset();
}

void FMetaHumanCalibrationErrorCalculator::Update(const TArray<FDetectedFeatures>& InDetectedFeaturesArray)
{
	for (const FDetectedFeatures& DetectedFeatures : InDetectedFeaturesArray)
	{
		Update(DetectedFeatures);
	}
}

void FMetaHumanCalibrationErrorCalculator::Update(const FDetectedFeatures& InDetectedPoints)
{
	for (int32 CameraIndex = 0; CameraIndex < CameraNames.Num(); ++CameraIndex)
	{
		const TArray<FBlockInfo>& ArrayOfBlocksForCamera = ArrayOfBlocks[CameraNames[CameraIndex]];
		TMap<int32, TArray<FErrors>>& ErrorsPerFramePerBlockForCamera = ErrorsPerCameraPerFramePerBlock[CameraNames[CameraIndex]];

		const TArray<FVector2D>& CameraPoints = InDetectedPoints.CameraPoints[CameraIndex].Points;
		const TArray<FVector2D>& ReprojectedPoints = InDetectedPoints.Points3dReprojected[CameraIndex].Points;

		check(CameraPoints.Num() == ReprojectedPoints.Num());

		ErrorsPerFramePerBlockForCamera.Add(InDetectedPoints.FrameIndex).Init(FErrors(), ArrayOfBlocksForCamera.Num());
		TArray<FErrors>& ErrorsPerBlockForCamera = ErrorsPerFramePerBlockForCamera[InDetectedPoints.FrameIndex];

		for (int32 BlockIndex = 0; BlockIndex < ArrayOfBlocksForCamera.Num(); ++BlockIndex)
		{
			TArray<double> Errors;

			for (int32 PointIndex = 0; PointIndex < CameraPoints.Num(); ++PointIndex)
			{
				if (!AreaOfInterest[CameraIndex].IsInside(CameraPoints[PointIndex]))
				{
					continue;
				}

				if (ArrayOfBlocksForCamera[BlockIndex].Box.IsInside(CameraPoints[PointIndex]))
				{
					FVector2D PointDiff = CameraPoints[PointIndex] - ReprojectedPoints[PointIndex];
					double Error = FMath::Sqrt(PointDiff.X * PointDiff.X + PointDiff.Y * PointDiff.Y);
					Errors.Add(Error);
				}
			}

			if (!Errors.IsEmpty())
			{
				FErrors ErrorsForFrame = CalculateErrors(Errors);

				check(ErrorsPerBlockForCamera[BlockIndex].Errors.IsEmpty());
				ErrorsPerBlockForCamera[BlockIndex] = ErrorsForFrame;
			}
		}
	}
}

double FMetaHumanCalibrationErrorCalculator::GetTotalRMSError() const
{
	if (ErrorsPerCameraPerFramePerBlock.IsEmpty())
	{
		return 0.0;
	}

	TArray<int32> FrameIndices;
	ErrorsPerCameraPerFramePerBlock.CreateConstIterator()->Value.GenerateKeyArray(FrameIndices);

	if (FrameIndices.IsEmpty())
	{
		return 0.0;
	}

	double TotalRMSError = 0.0;
	for (int32 FrameIndex : FrameIndices)
	{
		TotalRMSError += GetRMSErrorForFrame(FrameIndex);
	}

	return TotalRMSError / FrameIndices.Num();
}

double FMetaHumanCalibrationErrorCalculator::GetTotalMeanError() const
{
	if (ErrorsPerCameraPerFramePerBlock.IsEmpty())
	{
		return 0.0;
	}

	TArray<int32> FrameIndices;
	ErrorsPerCameraPerFramePerBlock.CreateConstIterator()->Value.GenerateKeyArray(FrameIndices);

	if (FrameIndices.IsEmpty())
	{
		return 0.0;
	}

	double TotalMeanError = 0.0;
	for (int32 FrameIndex : FrameIndices)
	{
		TotalMeanError += GetMeanErrorForFrame(FrameIndex);
	}

	return TotalMeanError / FrameIndices.Num();
}

double FMetaHumanCalibrationErrorCalculator::GetRMSErrorForFrame(int32 InFrameIndex) const
{
	double FrameRMSError = 0.0;
	int32 FramePointCount = 0;
	for (const FString& CameraName : CameraNames)
	{
		const TMap<int32, TArray<FErrors>>& ErrorsPerBlockForCamera = ErrorsPerCameraPerFramePerBlock[CameraName];
		check(ErrorsPerBlockForCamera.Contains(InFrameIndex));

		for (const FErrors& ErrorType : ErrorsPerBlockForCamera[InFrameIndex])
		{
			FrameRMSError += (ErrorType.RMSError * ErrorType.RMSError) * ErrorType.Errors.Num();
			FramePointCount += ErrorType.Errors.Num();
		}
	}

	if (FramePointCount == 0)
	{
		return 0.0;
	}

	return FMath::Sqrt(FrameRMSError / FramePointCount);
}

double FMetaHumanCalibrationErrorCalculator::GetMeanErrorForFrame(int32 InFrameIndex) const
{
	double FrameMeanError = 0.0;
	int32 FramePointCount = 0;
	for (const FString& CameraName : CameraNames)
	{
		const TMap<int32, TArray<FErrors>>& ErrorsPerBlockForCamera = ErrorsPerCameraPerFramePerBlock[CameraName];
		check(ErrorsPerBlockForCamera.Contains(InFrameIndex));

		for (const FErrors& ErrorType : ErrorsPerBlockForCamera[InFrameIndex])
		{
			FrameMeanError += ErrorType.MeanError * ErrorType.Errors.Num();
			FramePointCount += ErrorType.Errors.Num();
		}
	}

	if (FramePointCount == 0)
	{
		return 0.0;
	}

	return FrameMeanError / FramePointCount;
}

double FMetaHumanCalibrationErrorCalculator::GetMedianErrorForFrame(int32 InFrameIndex) const
{
	TArray<double> FrameErrors;
	for (const FString& CameraName : CameraNames)
	{
		const TMap<int32, TArray<FErrors>>& ErrorsPerBlockForCamera = ErrorsPerCameraPerFramePerBlock[CameraName];
		check(ErrorsPerBlockForCamera.Contains(InFrameIndex));

		for (const FErrors& ErrorType : ErrorsPerBlockForCamera[InFrameIndex])
		{
			FrameErrors.Append(ErrorType.Errors);
		}
	}

	if (FrameErrors.IsEmpty())
	{
		return 0.0;
	}

	return CalculateMedian(FrameErrors);
}

FMetaHumanCalibrationErrorCalculator::FErrors FMetaHumanCalibrationErrorCalculator::GetErrorsForFrame(int32 InFrameIndex) const
{
	FErrors Errors;
	Errors.RMSError = GetRMSErrorForFrame(InFrameIndex);
	Errors.MeanError = GetMeanErrorForFrame(InFrameIndex);

	TArray<double> FrameErrors;
	for (const FString& CameraName : CameraNames)
	{
		const TMap<int32, TArray<FErrors>>& ErrorsPerBlockForCamera = ErrorsPerCameraPerFramePerBlock[CameraName];
		check(ErrorsPerBlockForCamera.Contains(InFrameIndex));

		for (const FErrors& ErrorType : ErrorsPerBlockForCamera[InFrameIndex])
		{
			FrameErrors.Append(ErrorType.Errors);
		}
	}

	Errors.MedianError = CalculateMedian(FrameErrors);
	Errors.P90Error = CalculateP90(FrameErrors);
	Errors.Errors = MoveTemp(FrameErrors);

	return Errors;
}

double FMetaHumanCalibrationErrorCalculator::GetRMSErrorForFrame(const FString& InCameraName, int32 InFrameIndex) const
{
	check(ErrorsPerCameraPerFramePerBlock.Contains(InCameraName));

	double FrameRMSError = 0.0;
	int32 FramePointCount = 0;

	const TMap<int32, TArray<FErrors>>& ErrorsPerBlockForCamera = ErrorsPerCameraPerFramePerBlock[InCameraName];
	check(ErrorsPerBlockForCamera.Contains(InFrameIndex));

	for (const FErrors& ErrorType : ErrorsPerBlockForCamera[InFrameIndex])
	{
		FrameRMSError += (ErrorType.RMSError * ErrorType.RMSError) * ErrorType.Errors.Num();
		FramePointCount += ErrorType.Errors.Num();
	}

	if (FramePointCount == 0)
	{
		return 0.0;
	}

	return FMath::Sqrt(FrameRMSError / FramePointCount);
}

double FMetaHumanCalibrationErrorCalculator::GetMeanErrorForFrame(const FString& InCameraName, int32 InFrameIndex) const
{
	check(ErrorsPerCameraPerFramePerBlock.Contains(InCameraName));

	double FrameMeanError = 0.0;
	int32 FramePointCount = 0;

	const TMap<int32, TArray<FErrors>>& ErrorsPerBlockForCamera = ErrorsPerCameraPerFramePerBlock[InCameraName];
	check(ErrorsPerBlockForCamera.Contains(InFrameIndex));

	for (const FErrors& ErrorType : ErrorsPerBlockForCamera[InFrameIndex])
	{
		FrameMeanError += ErrorType.MeanError * ErrorType.Errors.Num();
		FramePointCount += ErrorType.Errors.Num();
	}

	if (FramePointCount == 0)
	{
		return 0.0;
	}

	return FrameMeanError / FramePointCount;
}

double FMetaHumanCalibrationErrorCalculator::GetMedianErrorForFrame(const FString& InCameraName, int32 InFrameIndex) const
{
	check(ErrorsPerCameraPerFramePerBlock.Contains(InCameraName));

	TArray<double> FrameErrors;
	const TMap<int32, TArray<FErrors>>& ErrorsPerBlockForCamera = ErrorsPerCameraPerFramePerBlock[InCameraName];
	check(ErrorsPerBlockForCamera.Contains(InFrameIndex));

	for (const FErrors& ErrorType : ErrorsPerBlockForCamera[InFrameIndex])
	{
		FrameErrors.Append(ErrorType.Errors);
	}

	if (FrameErrors.IsEmpty())
	{
		return 0.0;
	}

	return CalculateMedian(FrameErrors);
}

FMetaHumanCalibrationErrorCalculator::FErrors FMetaHumanCalibrationErrorCalculator::GetErrorsForFrame(const FString& InCameraName, int32 InFrameIndex) const
{
	FErrors Errors;
	Errors.RMSError = GetRMSErrorForFrame(InCameraName, InFrameIndex);
	Errors.MeanError = GetMeanErrorForFrame(InCameraName, InFrameIndex);

	TArray<double> FrameErrors;
	const TMap<int32, TArray<FErrors>>& ErrorsPerBlockForCamera = ErrorsPerCameraPerFramePerBlock[InCameraName];
	check(ErrorsPerBlockForCamera.Contains(InFrameIndex));

	for (const FErrors& ErrorType : ErrorsPerBlockForCamera[InFrameIndex])
	{
		FrameErrors.Append(ErrorType.Errors);
	}

	Errors.MedianError = CalculateMedian(FrameErrors);
	Errors.P90Error = CalculateP90(FrameErrors);
	Errors.Errors = MoveTemp(FrameErrors);

	return Errors;;
}

double FMetaHumanCalibrationErrorCalculator::GetRMSErrorForBlock(const FString& InCameraName, int32 InBlockIndex, int32 InFrameIndex) const
{
	check(ErrorsPerCameraPerFramePerBlock.Contains(InCameraName));

	if (ErrorsPerCameraPerFramePerBlock[InCameraName].Contains(InFrameIndex))
	{
		check(ErrorsPerCameraPerFramePerBlock[InCameraName][InFrameIndex].IsValidIndex(InBlockIndex));
		return ErrorsPerCameraPerFramePerBlock[InCameraName][InFrameIndex][InBlockIndex].RMSError;
	}

	return 0.0;
}

double FMetaHumanCalibrationErrorCalculator::GetMeanErrorForBlock(const FString& InCameraName, int32 InBlockIndex, int32 InFrameIndex) const
{
	check(ErrorsPerCameraPerFramePerBlock.Contains(InCameraName));

	if (ErrorsPerCameraPerFramePerBlock[InCameraName].Contains(InFrameIndex))
	{
		check(ErrorsPerCameraPerFramePerBlock[InCameraName][InFrameIndex].IsValidIndex(InBlockIndex));
		return ErrorsPerCameraPerFramePerBlock[InCameraName][InFrameIndex][InBlockIndex].MeanError;
	}

	return 0.0;
}

double FMetaHumanCalibrationErrorCalculator::GetMedianErrorForBlock(const FString& InCameraName, int32 InBlockIndex, int32 InFrameIndex) const
{
	check(ErrorsPerCameraPerFramePerBlock.Contains(InCameraName));

	if (ErrorsPerCameraPerFramePerBlock[InCameraName].Contains(InFrameIndex))
	{
		check(ErrorsPerCameraPerFramePerBlock[InCameraName][InFrameIndex].IsValidIndex(InBlockIndex));
		return ErrorsPerCameraPerFramePerBlock[InCameraName][InFrameIndex][InBlockIndex].MedianError;
	}

	return 0.0;
}

double FMetaHumanCalibrationErrorCalculator::GetP90ErrorForBlock(const FString& InCameraName, int32 InBlockIndex, int32 InFrameIndex) const
{
	check(ErrorsPerCameraPerFramePerBlock.Contains(InCameraName));

	if (ErrorsPerCameraPerFramePerBlock[InCameraName].Contains(InFrameIndex))
	{
		check(ErrorsPerCameraPerFramePerBlock[InCameraName][InFrameIndex].IsValidIndex(InBlockIndex));
		return ErrorsPerCameraPerFramePerBlock[InCameraName][InFrameIndex][InBlockIndex].P90Error;
	}

	return 0.0;
}

FMetaHumanCalibrationErrorCalculator::FErrors FMetaHumanCalibrationErrorCalculator::GetErrorsForBlock(const FString& InCameraName, int32 InBlockIndex, int32 InFrameIndex) const
{
	FErrors Errors;
	Errors.RMSError = GetRMSErrorForBlock(InCameraName, InBlockIndex, InFrameIndex);
	Errors.MeanError = GetMeanErrorForBlock(InCameraName, InBlockIndex, InFrameIndex);
	Errors.MedianError = GetMedianErrorForBlock(InCameraName, InBlockIndex, InFrameIndex);
	Errors.P90Error = GetP90ErrorForBlock(InCameraName, InBlockIndex, InFrameIndex);
	Errors.Errors = ErrorsPerCameraPerFramePerBlock[InCameraName][InFrameIndex][InBlockIndex].Errors;

	return Errors;
}

TArray<int32> FMetaHumanCalibrationErrorCalculator::GetBlockIndices(const FString& InCameraName) const
{
	check(ArrayOfBlocks.Contains(InCameraName));

	const TArray<FBlockInfo>& ArrayOfBlocksForCamera = ArrayOfBlocks[InCameraName];

	TArray<int32> BlockIndices;
	for (int32 Index = 0; Index < ArrayOfBlocksForCamera.Num(); ++Index)
	{
		BlockIndices.Add(Index);
	}

	return BlockIndices;
}

FVector2D FMetaHumanCalibrationErrorCalculator::GetBlockSize() const
{
	return AreaOfInterest[0].Max / CoverageMapSize;
}

FBox2D FMetaHumanCalibrationErrorCalculator::GetBlock(const FString& InCameraName, int32 InBlockIndex) const
{
	check(ArrayOfBlocks.Contains(InCameraName));
	check(ArrayOfBlocks[InCameraName].IsValidIndex(InBlockIndex));

	return ArrayOfBlocks[InCameraName][InBlockIndex].Box;
}

int32 FMetaHumanCalibrationErrorCalculator::GetBlockNum() const
{
	return ArrayOfBlocks.CreateConstIterator()->Value.Num();
}

TArray<FBox2D> FMetaHumanCalibrationErrorCalculator::GetAreaOfInterest() const
{
	return AreaOfInterest;
}

TArray<FIntVector2> FMetaHumanCalibrationErrorCalculator::GetImageSizes() const
{
	return ImageSizes;
}

TArray<FString> FMetaHumanCalibrationErrorCalculator::GetCameraNames() const
{
	return CameraNames;
}

void FMetaHumanCalibrationErrorCalculator::ToggleMarkBlock(const FString& InCameraName, int32 InBlockIndex)
{
	check(ArrayOfBlocks.Contains(InCameraName));
	check(ArrayOfBlocks[InCameraName].IsValidIndex(InBlockIndex));

	bool bIsMarked = ArrayOfBlocks[InCameraName][InBlockIndex].bIsMarked;
	ArrayOfBlocks[InCameraName][InBlockIndex].bIsMarked = !bIsMarked;
}

bool FMetaHumanCalibrationErrorCalculator::IsBlockMarked(const FString& InCameraName, int32 InBlockIndex) const
{
	check(ArrayOfBlocks.Contains(InCameraName));
	check(ArrayOfBlocks[InCameraName].IsValidIndex(InBlockIndex));

	return ArrayOfBlocks[InCameraName][InBlockIndex].bIsMarked;
}

void FMetaHumanCalibrationErrorCalculator::Reset()
{
	for (int32 Index = 0; Index < CameraNames.Num(); ++Index)
	{
		int32 XSize = (AreaOfInterest[Index].Max.X - AreaOfInterest[Index].Min.X) / CoverageMapSize.X;
		int32 YSize = (AreaOfInterest[Index].Max.Y - AreaOfInterest[Index].Min.Y) / CoverageMapSize.Y;

		TArray<FBlockInfo>& CameraBlocks = ArrayOfBlocks.Add(CameraNames[Index]);

		for (int32 YIndex = 0; YIndex < CoverageMapSize.Y; ++YIndex)
		{
			for (int32 XIndex = 0; XIndex < CoverageMapSize.X; ++XIndex)
			{
				FVector2D TopLeft(XIndex * XSize + AreaOfInterest[Index].Min.X, YIndex * YSize + AreaOfInterest[Index].Min.Y);
				FVector2D BottomRight((XIndex + 1) * XSize + AreaOfInterest[Index].Min.X, (YIndex + 1) * YSize + AreaOfInterest[Index].Min.Y);
				FBox2D Block(MoveTemp(TopLeft), MoveTemp(BottomRight));
				CameraBlocks.Add({ MoveTemp(Block), false });
			}
		}

		ErrorsPerCameraPerFramePerBlock.Add(CameraNames[Index]);
	}
}

void FMetaHumanCalibrationErrorCalculator::InvalidateForCamera(const FString& InCameraName)
{
	ArrayOfBlocks.Remove(InCameraName);
	ErrorsPerCameraPerFramePerBlock.Remove(InCameraName);

	ResetForCamera(InCameraName);
}

void FMetaHumanCalibrationErrorCalculator::ResetForCamera(const FString& InCameraName)
{
	int32 Index = CameraNames.IndexOfByKey(InCameraName);
	check(Index != INDEX_NONE);

	int32 XSize = (AreaOfInterest[Index].Max.X - AreaOfInterest[Index].Min.X) / CoverageMapSize.X;
	int32 YSize = (AreaOfInterest[Index].Max.Y - AreaOfInterest[Index].Min.Y) / CoverageMapSize.Y;

	TArray<FBlockInfo>& CameraBlocks = ArrayOfBlocks.Add(CameraNames[Index]);

	for (int32 YIndex = 0; YIndex < CoverageMapSize.Y; ++YIndex)
	{
		for (int32 XIndex = 0; XIndex < CoverageMapSize.X; ++XIndex)
		{
			FVector2D TopLeft(XIndex * XSize + AreaOfInterest[Index].Min.X, YIndex * YSize + AreaOfInterest[Index].Min.Y);
			FVector2D BottomRight((XIndex + 1) * XSize + AreaOfInterest[Index].Min.X, (YIndex + 1) * YSize + AreaOfInterest[Index].Min.Y);
			FBox2D Block(MoveTemp(TopLeft), MoveTemp(BottomRight));
			CameraBlocks.Add({ MoveTemp(Block), false });
		}
	}

	ErrorsPerCameraPerFramePerBlock.Add(CameraNames[Index]);
}

FMetaHumanCalibrationErrorCalculator::FErrors FMetaHumanCalibrationErrorCalculator::CalculateErrors(const TArray<double>& InErrors)
{
	FErrors ErrorsForFrame;

	for (double Error : InErrors)
	{
		ErrorsForFrame.MeanError += Error;
		ErrorsForFrame.RMSError += Error * Error;
	}

	ErrorsForFrame.MeanError /= InErrors.Num();
	ErrorsForFrame.RMSError = FMath::Sqrt(ErrorsForFrame.RMSError / InErrors.Num());

	ErrorsForFrame.MedianError = CalculateMedian(InErrors);
	ErrorsForFrame.P90Error = CalculateP90(InErrors);
	ErrorsForFrame.Errors = InErrors;

	return ErrorsForFrame;
}

double FMetaHumanCalibrationErrorCalculator::CalculateMedian(const TArray<double>& InErrors)
{
	if (InErrors.IsEmpty())
	{
		return 0.0;
	}

	TArray<double> SortedErrors = InErrors;
	SortedErrors.Sort();

	int32 Size = SortedErrors.Num();
	if (Size % 2 != 0)
	{
		return SortedErrors[Size / 2];
	}

	return (SortedErrors[(Size / 2) - 1] + SortedErrors[Size / 2]) / 2.0;
}

double FMetaHumanCalibrationErrorCalculator::CalculateP90(const TArray<double>& InErrors)
{
	if (InErrors.IsEmpty())
	{
		return 0.0;
	}

	TArray<double> SortedErrors = InErrors;
	SortedErrors.Sort();

	int32 Size = SortedErrors.Num();
	int32 IndexOf90th = FMath::FloorToInt32((Size - 1) * 0.9);

	check(SortedErrors.IsValidIndex(IndexOf90th));
	return SortedErrors[IndexOf90th];
}

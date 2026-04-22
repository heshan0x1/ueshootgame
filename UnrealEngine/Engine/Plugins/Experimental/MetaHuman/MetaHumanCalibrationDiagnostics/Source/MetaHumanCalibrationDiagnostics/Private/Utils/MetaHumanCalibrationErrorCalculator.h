// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Math/Box2D.h"
#include "Math/Vector2D.h"

#include "Containers/Map.h"
#include "Containers/Array.h"
#include "Containers/UnrealString.h"

#include "UMetaHumanRobustFeatureMatcher.h"

class FMetaHumanCalibrationErrorCalculator
{
public:

	struct FErrors
	{
		double RMSError = 0.0;
		double MeanError = 0.0;
		double MedianError = 0.0;
		double P90Error = 0.0;

		TArray<double> Errors;
	};

	FMetaHumanCalibrationErrorCalculator(const FVector2D& InCoverageMapSize,
										 const TArray<FString>& InCameraNames,
										 const TArray<FIntVector2>& InImageSizes);

	void SetAreaOfInterestForCamera(const FString& InCameraName, const FBox2D& InAreaOfInterest);

	bool ContainsErrors() const;
	bool ContainsErrors(int32 InFrame) const;

	void Invalidate(TOptional<FVector2D> InCoverageMapSize = TOptional<FVector2D>());

	void Update(const TArray<FDetectedFeatures>& InDetectedFeaturesArray);
	void Update(const FDetectedFeatures& InDetectedFeatures);

	double GetTotalRMSError() const;
	double GetTotalMeanError() const;

	double GetRMSErrorForFrame(int32 InFrameIndex) const;
	double GetMeanErrorForFrame(int32 InFrameIndex) const;
	double GetMedianErrorForFrame(int32 InFrameIndex) const;
	FErrors GetErrorsForFrame(int32 InFrameIndex) const;

	double GetRMSErrorForFrame(const FString& InCameraName, int32 InFrameIndex) const;
	double GetMeanErrorForFrame(const FString& InCameraName, int32 InFrameIndex) const;
	double GetMedianErrorForFrame(const FString& InCameraName, int32 InFrameIndex) const;
	FErrors GetErrorsForFrame(const FString& InCameraName, int32 InFrameIndex) const;

	double GetRMSErrorForBlock(const FString& InCameraName, int32 InBlockIndex, int32 InFrameIndex) const;
	double GetMeanErrorForBlock(const FString& InCameraName, int32 InBlockIndex, int32 InFrameIndex) const;
	double GetMedianErrorForBlock(const FString& InCameraName, int32 InBlockIndex, int32 InFrameIndex) const;
	double GetP90ErrorForBlock(const FString& InCameraName, int32 InBlockIndex, int32 InFrameIndex) const;
	FErrors GetErrorsForBlock(const FString& InCameraName, int32 InBlockIndex, int32 InFrameIndex) const;

	TArray<int32> GetBlockIndices(const FString& InCameraName) const;

	FVector2D GetBlockSize() const;
	FBox2D GetBlock(const FString& InCameraName, int32 InBlockIndex) const;
	int32 GetBlockNum() const;

	TArray<FBox2D> GetAreaOfInterest() const;
	TArray<FIntVector2> GetImageSizes() const;
	TArray<FString> GetCameraNames() const;

	void ToggleMarkBlock(const FString& InCameraName, int32 InBlockIndex);
	bool IsBlockMarked(const FString& InCameraName, int32 InBlockIndex) const;

private:

	void Reset();
	void InvalidateForCamera(const FString& InCameraName);
	void ResetForCamera(const FString& InCameraName);

	static FErrors CalculateErrors(const TArray<double>& InErrors);
	static double CalculateMedian(const TArray<double>& InErrors);
	static double CalculateP90(const TArray<double>& InErrors);

	FVector2D CoverageMapSize;
	TArray<FString> CameraNames;
	TArray<FIntVector2> ImageSizes;
	TArray<FBox2D> AreaOfInterest;

	struct FBlockInfo
	{
		FBox2D Box;
		bool bIsMarked = false;
	};

	TMap<FString, TArray<FBlockInfo>> ArrayOfBlocks;
	TMap<FString, TMap<int32, TArray<FErrors>>> ErrorsPerCameraPerFramePerBlock;
};
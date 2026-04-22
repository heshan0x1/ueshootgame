// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Array.h"
#include "CaptureData.h"

#include "Misc/NotNull.h"

#define UE_API METAHUMANCALIBRATIONCORE_API

struct FMetaHumanCalibrationFramePaths
{
	FString FirstCamera;
	FString SecondCamera;
};

class FMetaHumanCalibrationFrameResolver
{
public:
	class FCameraDescriptor
	{
	public:

		UE_API FCameraDescriptor(TArray<FString> InImagePaths, FTimecode InTimecode, FFrameRate InFrameRate);

		UE_API const TArray<FString>& GetImagePaths() const;
		UE_API FTimecode GetTimecode() const;
		UE_API FFrameRate GetFrameRate() const;

	private:
		const TArray<FString> ImagePaths;
		const FTimecode Timecode;
		const FFrameRate FrameRate;
	};

	UE_API static TOptional<FMetaHumanCalibrationFrameResolver> CreateFromCaptureData(const UFootageCaptureData* InCaptureData);

	UE_API FMetaHumanCalibrationFrameResolver(const FCameraDescriptor& InFirstCamera, 
											  const FCameraDescriptor& InSecondCamera);

	/** 
    * @brief Gets the paths to all the frames for the specified camera index.
    * @returns true if the camera index is valid and false otherwise. 
    */
	UE_API bool GetFramePathsForCameraIndex(int32 InCameraIndex, TArray<FString>& OutFramePaths) const;

	/**
    * @brief Gets the calibration frame paths for the specified frame index.
    * @returns true if the frame index is valid and false otherwise.
    */
	UE_API bool GetCalibrationFramePathsForFrameIndex(int32 InFrameIndex, FMetaHumanCalibrationFramePaths& OutCalibrationFrame) const;

	/** @brief Gets the calibration frame paths for every frame */
	UE_API TArray<FMetaHumanCalibrationFramePaths> GetCalibrationFramePaths() const;

	/** @returns true if the resolver has frames. */
	UE_API bool HasFrames() const;

private:

	FMetaHumanCalibrationFrameResolver(const UFootageCaptureData* InCaptureData);

	TArray<FMetaHumanCalibrationFramePaths> CalibrationFramePaths;
};

#undef UE_API
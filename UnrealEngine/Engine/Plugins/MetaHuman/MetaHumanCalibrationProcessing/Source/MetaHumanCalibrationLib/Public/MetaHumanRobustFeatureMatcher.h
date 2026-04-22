// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Templates/PimplPtr.h"
#include "CameraCalibration.h"

#define UE_API METAHUMANCALIBRATIONLIB_API

namespace UE
{
namespace Wrappers
{

class FMetaHumanRobustFeatureMatcher
{
public:

	UE_API FMetaHumanRobustFeatureMatcher();

	/**
	* Initialize robust feature matcher.
	* @param[in] InCameraCalibrations An array of camera calibrations
	* @param[in] InReprojectionThreshold Max allowed reprojection error (in pixels) to consider a 3D-2D correspondence
	* @param[in] InRatioThreshold Ratio used for comparing best match distance to second-best; keeps match if sufficiently better
	* @returns True if initialization is successful, False otherwise.
	*/
	UE_API bool Init(const TArray<FCameraCalibration>& InCameraCalibrations,
					 double InReprojectionThreshold = 5.0, 
					 double InRatioThreshold = 0.75);

	/**
	* Initialize robust feature matcher.
	* @param[in] InCameraCalibrationFile Camera Calibration file
	* @param[in] InReprojectionThreshold Max allowed reprojection error (in pixels) to consider a 3D-2D correspondence
	* @param[in] InRatioThreshold Ratio used for comparing best match distance to second-best; keeps match if sufficiently better
	* @returns True if initialization is successful, False otherwise.
	*/
	UE_INTERNAL UE_API bool Init(const FString& InCameraCalibrationFile,
								 double InReprojectionThreshold = 5.0,
								 double InRatioThreshold = 0.75);

	/**
	* Adds a footage information for the camera.
	* @param[in] InCameraName Name of the camera used for calibration
	* @param[in] InWidth Image width
	* @param[in] InHeight Image height
	* @returns True if adding is successful, False otherwise.
	*/
	UE_API bool AddCamera(const FString& InCameraName, int32 InWidth, int32 InHeight);

	/**
	* Detects the matching features for the cameras.
	* @param[in] InFrame Frame index
	* @param[in] InImages Image data for all cameras for a specified frame
	* @returns True if matching is successful, False otherwise.
	*/
	UE_API bool DetectFeatures(int64 InFrame, const TArray<const unsigned char*>& InImages);

	/**
	* Provides the matching features for the cameras.
	* @param[in] InFrame Frame index
	* @param[out] OutPoints3d Triangulated 3D coordinates of calibration target points in stereo camera frame
	* @param[out] OutCameraPoints 2D detected points in each image
	* @param[out] OutPoints3dReprojected 2D points obtained by projecting Points3D back into the image using camera calibration
	* @returns True if matching is successful, False otherwise.
	*/
	UE_API bool GetFeatures(int64 InFrame, 
							TArray<FVector2D>& OutPoints3d,
							TArray<TArray<FVector2D>>& OutCameraPoints,
							TArray<TArray<FVector2D>>& OutPoints3dReprojected);

private:

	struct FPrivate;
	TPimplPtr<FPrivate> ImplPtr;
};

}
}

#undef UE_API
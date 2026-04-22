// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanRobustFeatureMatcher.h"

#include "api/RobustFeatureMatcher.h"
#include "SetCamerasHelper.h"

namespace UE
{
namespace Wrappers
{

namespace Private
{

TArray<FVector2D> PointsVectorToTArrayPoints(const std::vector<float>& InPoints)
{
	TArray<FVector2D> Points;

	std::size_t PointsSize = InPoints.size() / 2;
	Points.Reserve(PointsSize);

	for (std::size_t PointIndex = 0; PointIndex < PointsSize; ++PointIndex)
	{
		double X = InPoints[PointIndex * 2];
		double Y = InPoints[PointIndex * 2 + 1];

		Points.Emplace(X, Y);
	}

	return Points;
}

}

struct FMetaHumanRobustFeatureMatcher::FPrivate
{
	TITAN_API_NAMESPACE::RobustFeatureMatcher API;
};

FMetaHumanRobustFeatureMatcher::FMetaHumanRobustFeatureMatcher()
	: ImplPtr(MakePimpl<FPrivate>())
{
}

bool FMetaHumanRobustFeatureMatcher::Init(const TArray<FCameraCalibration>& InCameraCalibrations,
										  double InReprojectionThreshold,
										  double InRatioThreshold)
{
	std::map<std::string, TITAN_API_NAMESPACE::OpenCVCamera> CameraCalibrations;
	SetCamerasHelper(InCameraCalibrations, CameraCalibrations);

	return ImplPtr->API.Init(CameraCalibrations, InReprojectionThreshold, InRatioThreshold);
}

bool FMetaHumanRobustFeatureMatcher::Init(const FString& InCameraCalibrationFile,
										  double InReprojectionThreshold,
										  double InRatioThreshold)
{
	return ImplPtr->API.Init(TCHAR_TO_ANSI(*InCameraCalibrationFile), InReprojectionThreshold, InRatioThreshold);
}

bool FMetaHumanRobustFeatureMatcher::AddCamera(const FString& InCameraName, int32 InWidth, int32 InHeight)
{
	return ImplPtr->API.AddCamera(TCHAR_TO_ANSI(*InCameraName), InWidth, InHeight);
}

bool FMetaHumanRobustFeatureMatcher::DetectFeatures(int64 InFrame, 
													const TArray<const unsigned char*>& InImages)
{
	std::vector<const unsigned char*> Images;
	Images.reserve(InImages.Num());

	for (const unsigned char* Image : InImages)
	{
		Images.push_back(Image);
	}

	return ImplPtr->API.DetectFeatures(InFrame, Images);
}

bool FMetaHumanRobustFeatureMatcher::GetFeatures(int64 InFrame,
												 TArray<FVector2D>& OutPoints3d,
												 TArray<TArray<FVector2D>>& OutCameraPoints,
												 TArray<TArray<FVector2D>>& OutPoints3dReprojected)
{
	std::vector<float> Points3d;
	std::vector<std::vector<float>> CameraPoints;
	std::vector<std::vector<float>> Points3dReprojected;

	bool bFoundFeatures = ImplPtr->API.GetDetectedFeatures(InFrame, Points3d, CameraPoints, Points3dReprojected);

	if (!bFoundFeatures)
	{
		return false;
	}

	OutPoints3d.Reserve(Points3d.size());
	OutCameraPoints.Reserve(CameraPoints.size());
	OutPoints3dReprojected.Reserve(Points3dReprojected.size());

	OutPoints3d = Private::PointsVectorToTArrayPoints(Points3d);

	for (const std::vector<float>& Points : CameraPoints)
	{
		OutCameraPoints.Add(Private::PointsVectorToTArrayPoints(Points));
	}

	for (const std::vector<float>& Points : Points3dReprojected)
	{
		OutPoints3dReprojected.Add(Private::PointsVectorToTArrayPoints(Points));
	}

	return true;
}

}
}
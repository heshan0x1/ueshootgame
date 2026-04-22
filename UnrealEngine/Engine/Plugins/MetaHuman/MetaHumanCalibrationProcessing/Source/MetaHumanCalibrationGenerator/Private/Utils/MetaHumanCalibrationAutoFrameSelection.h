// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Map.h"
#include "Containers/Array.h"

#include "Math/Box2D.h"

#include "MetaHumanCalibrationPatternDetector.h"

class FMetaHumanCalibrationAutoFrameSelection
{
public:

	using FDetectedFrame = FMetaHumanCalibrationPatternDetector::FDetectedFrame;
	using FPatternInfo = FMetaHumanCalibrationPatternDetector::FPatternInfo;

	FMetaHumanCalibrationAutoFrameSelection(TPair<FString, FString> InCameraNames,
											TPair<FIntVector2, FIntVector2> InImageSizes,
											TPair<FBox2D, FBox2D> InAreasOfInterest);

	TArray<int32> RunSelection(const FPatternInfo& InPattern,
							   const TMap<int32, FDetectedFrame>& InDetectedFrames);

private:

	class FImpl;
	TPimplPtr<FImpl> Impl;
};
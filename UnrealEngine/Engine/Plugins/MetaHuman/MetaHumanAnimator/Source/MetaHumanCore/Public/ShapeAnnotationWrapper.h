// Copyright Epic Games, Inc. All Rights Reserved.

// The purpose of this file it to define an interface to rlibv functionality that can
// be called by UE. Dont use dlib etc types here since that complicated the compile.

#pragma once

#include "CoreMinimal.h"
#include "MetaHumanContourData.h"

#define UE_API METAHUMANCORE_API

enum class ECurveDisplayMode : uint8
{
	Visualization,
	Editing
};

class FShapeAnnotationWrapper
{
public:
	UE_API FShapeAnnotationWrapper();
	UE_API ~FShapeAnnotationWrapper();

	/** Returns a list of control vertices for a curve. Start and end points are not included */
	UE_API TArray<FVector2D> GetControlVerticesForCurve(const TArray<FVector2D>& InLandmarkData, const FString& InCurveName, ECurveDisplayMode InDisplayMode) const;

	/** Returns point data that represents a Catmull-Rom spline, generated from contour data */
	UE_API TMap<FString, TArray<FVector2D>> GetDrawingSplinesFromContourData(const TObjectPtr<class UMetaHumanContourData> InContourData);

private:

	/** Initializes keypoints and keypoint curves in the form that rlibv::shapeAnnotation requires to generate curves */
	UE_API void InitializeShapeAnnotation(const TObjectPtr<class UMetaHumanContourData> InContourData, bool bUseDensePoints);

	class FImpl;
	TPimplPtr<FImpl> Impl;
};

#undef UE_API

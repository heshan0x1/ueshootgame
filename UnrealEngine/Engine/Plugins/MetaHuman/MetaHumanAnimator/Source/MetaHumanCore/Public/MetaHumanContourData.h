// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FrameTrackingContourData.h"
#include "MetaHumanContourData.generated.h"

#define UE_API METAHUMANCORE_API

USTRUCT()
struct FControlVertex
{
	GENERATED_BODY()

	UPROPERTY()
	FVector2D PointPosition = FVector2D::ZeroVector;

	UPROPERTY()
	TArray<FVector2D> LinePoints;

	UPROPERTY()
	TArray<FString> CurveNames;

	UPROPERTY()
	int32 PointId = INDEX_NONE;

	UPROPERTY()
	bool bIsSinglePointCurve = false;
};

USTRUCT()
struct FReducedContour
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FControlVertex> ControlVertices;
};

UCLASS(MinimalAPI)
class UMetaHumanContourData : public UObject
{
public:

	GENERATED_BODY()

	UE_API void SetContourDataForDrawing(const TMap<FString, TArray<FVector2D>>& InContoursDrawData);
	UE_API void SetFullCurveContourDataForDrawing(const TMap<FString, TArray<FVector2D>>& InFullContoursForDraw);

	/** Removes generated curve draw data and control vertices */
	UE_API void ClearGeneratedDrawData();

	/** Returns true if any of the curves (this control vertex is on) is visible */
	UE_API bool ControlVertexIsVisible(const FControlVertex& InVertex) const;

	/** Returns true if the curve is visible */
	UE_API bool ContourIsVisible(const FString& InCurveName) const;

	UE_API const TMap<FString, TArray<FVector2D>>& GetReducedDataForDrawing();
	UE_API const TMap<FString, TArray<FVector2D>>& GetTrackingContourDataForDrawing();
	
	/** Returns a reference to a control vertex for a specified point id */
	UE_API FControlVertex* GetControlVertexFromPointId(const int32 InPointId);
	
	/** Returns a copy of control vertices from reduced contour. End points NOT included */
	UE_API TArray<FControlVertex> GetControlVerticesForCurve(const FString& InCurveName);

	/** Returns positions of control vertices for the curve. End points NOT included */
	UE_API TArray<FVector2D> GetControlVertexPositions(const FString& InCurveName);

	/** Returns the list of point IDs for a curve INCLUDING the end points */
	UE_API TArray<int32> GetPointIdsWithEndpointsForCurve(const FString& InCurveName) const;

	/** Returns a list of curves that have selected status set to true */
	UE_API TSet<FString> GetSelectedCurves() const;

	/** returns a pair of start and end point names for a given curve */
	UE_API TPair<FString, FString> GetStartEndNamesForCurve(const FString& InCurveName) const;

	UPROPERTY()
	FFrameTrackingContourData FrameTrackingContourData;

	UPROPERTY()
	TMap<FString, FReducedContour> ReducedContourData;

	/** A list of curves that the user has manually adjusted */
	UPROPERTY()
	TSet<FString> ManuallyModifiedCurves;

	UPROPERTY()
	FString ContourDataConfigVersion = "";

private:

	/** Returns the list of point IDs for a curve. End points NOT included */
	UE_API const TArray<int32> GetPointIdsForCurve(const FString& InCurveName) const;

	/** A draw data for curves, generated from control vertices */
	TMap<FString, TArray<FVector2D>> CurveDrawDataFromReducedContours;
	
	/** A draw data for curves, generated from all points produced by contour tracker */
	TMap<FString, TArray<FVector2D>> CurveDrawDataFromTrackingContours;
};

#undef UE_API

// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MetaHumanContourData.h"
#include "ShapeAnnotationWrapper.h"
#include "Framework/DelayedDrag.h"

#define UE_API METAHUMANCORE_API

DECLARE_MULTICAST_DELEGATE_OneParam(FOnCurvesSelectedDelegate, bool bClearPointSelection)
DECLARE_DELEGATE_RetVal(TSet<int32>&, FOnGetViewportPointSelectionDelegate)

class FMetaHumanCurveDataController
{
public:

	UE_API FMetaHumanCurveDataController(TObjectPtr<UMetaHumanContourData> InCurveData, ECurveDisplayMode InMode = ECurveDisplayMode::Editing);

	/** Sets up the curve list from config along with default data to be displayed */
	UE_API void InitializeContoursFromConfig(const FFrameTrackingContourData& InDefaultContourData, const FString& InConfigVersion);

	/** Updates the tracking contour data present in config & relevant data to display those curves */
	UE_API void UpdateFromContourData(const FFrameTrackingContourData& InTrackingData, const bool bUpdateVisibility);

	/** Updates individual curves, keeping reduced data of other curves intact */
	UE_API void UpdateIndividualCurves(const FFrameTrackingContourData& InTrackingData);

	/** Removes all contour data, invalidating the initialization from config */
	UE_API void ClearContourData();

	/** Moves selected points by a provided offset */
	UE_API void OffsetSelectedPoints(const TSet<int32>& InSelectedPoints, const FVector2D& InOffset);

	/** Moves a single point to a mouse cursor in image space */
	UE_API void MoveSelectedPoint(const FVector2D& InNewPosition, const int32 InPointId);

	/** Update the original dense points data to represent the modified curve */
	UE_API void UpdateDensePointsAfterDragging(const TSet<int32>& InDraggedIds);

	/** Updates the selection of contour data & emits the signal for relevant updates */
	UE_API void SetCurveSelection(const TSet<FString>& InSelectedCurves, bool bClearPointSelection);

	/** Updates the selection of contour data based of individually selected points */
	UE_API void ResolveCurveSelectionFromSelectedPoints(const TSet<int32>& InSelectedPoints);

	/** Triggers relevant updates to draw data after the undo operation */
	UE_API void HandleUndoOperation();

	/** Clears displayed data but keeps controller initialization with whatever last data was set */
	UE_API void ClearDrawData();

	/** Resolves end point selection when these points belong to multiple curves */
	UE_API void ResolvePointSelectionOnCurveVisibilityChanged(const TArray<FString>& InCurveNames, bool bInSingleCurve, bool bInIsHiding);

	/** Checks if the curve is selected or active */
	UE_API TPair<bool, bool> GetCurveSelectedAndActiveStatus(const FString& InCurve);

	UE_API void GenerateCurvesFromControlVertices();
	UE_API void GenerateDrawDataForDensePoints();

	/** Scoped operation for adding or removing the key */
	UE_API bool AddRemoveKey(const FVector2D& InPointPosition, const FString& InCurveName, bool bInAdd);

	UE_API TArray<FString> GetCurveNamesForPointId(const int32 InPointId);
	UE_API TArray<int32> GetPointIdsWithEndPointsForCurve(const FString& InCurveName) const;
	UE_API TMap<FString, TArray<FVector2D>> GetDensePointsForVisibleCurves() const;
	UE_API TMap<FString, TArray<FVector2D>> GetFullSplineDataForVisibleCurves() const;
	UE_API TArray<FControlVertex> GetAllVisibleControlVertices();

	FSimpleMulticastDelegate& TriggerContourUpdate() { return UpdateContourDelegate; }
	FOnCurvesSelectedDelegate& GetCurvesSelectedDelegate() { return OnCurvesSelectedDelegate; }
	FOnGetViewportPointSelectionDelegate& ViewportPointSelectionRetrieverDelegate() { return OnGetViewportPointSelection; }

	const TObjectPtr<UMetaHumanContourData> GetContourData() { return ContourData; }

private:

	UE_API void CreateControlVertices();
	UE_API void RecreateControlVertexIds();
	UE_API void RecreateCurvesFromReducedData();
	UE_API void ClearCurveSelection();
	UE_API void GenerateCurveDataPostTrackingDataChange();
	UE_API void ModifyViewportEndPointSelectionForCurveVisibility(const FString& InCurveName, const FString& InEndPointName);

	UE_API bool CurveIsVisible(const FString& InCurveName) const;
	UE_API int32 GetCurveInsertionIndex(const FVector2D& InInsertionPos, const FString& InCurveName);
	UE_API float GetDistanceToNearestVertex(const FVector2D& InPosition, const FString& InCurveName, int32& outIndex);

	UE_API TArray<FString> GetCurveNamesForEndPoints(const FString& InEndPointName) const;
	UE_API TArray<FVector2D> GetPointAtPosition(const FVector2D& InScreenPosition) const;
	UE_API TMap<FString, TArray<FVector2D>> GetCurveDisplayDataForEditing() const;
	UE_API TMap<FString, TArray<FVector2D>> GetCurveDisplayDataForVisualization() const;
	UE_API FReducedContour GetReducedContourForTrackingContour(const TPair<FString, FTrackingContour>& InContour);

	TObjectPtr<UMetaHumanContourData> ContourData;	
	FShapeAnnotationWrapper ShapeAnnotationWrapper;

	static constexpr int32 LinesPerCircle = 33;
	static constexpr int32 PointSize = 5;
	static constexpr float CurveAddRemoveThreshold = 2.5f;
	static constexpr float SelectionCaptureRange = 40.0f;

	const ECurveDisplayMode DisplayMode;
	FSimpleMulticastDelegate UpdateContourDelegate;
	FOnCurvesSelectedDelegate OnCurvesSelectedDelegate;
	FOnGetViewportPointSelectionDelegate OnGetViewportPointSelection;
};

#undef UE_API

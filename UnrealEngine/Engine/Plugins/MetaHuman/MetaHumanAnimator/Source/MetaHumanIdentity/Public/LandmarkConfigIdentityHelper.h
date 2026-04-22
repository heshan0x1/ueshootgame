// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "FrameTrackingContourData.h"

#define UE_API METAHUMANIDENTITY_API

enum class EIdentityPoseType : uint8;
enum class EIdentityPartMeshes : uint8;

enum class ECurvePresetType : uint8
{
	Invalid = 0,
	Identity_NeutralPose,
	Identity_TeethPose,
	Performance
};

/** persistent data for curves, loaded from json */
struct FMarkerCurveDef
{
	FString Name;
	FString StartPointName;
	FString EndPointName;

	TArray<int32> VertexIDs;
	TArray<FVector2D> DefaultScreenPoints;
	TArray<FString> GroupTagIDs;

	FString CurveMeshFromConfig;
};

struct FMarkerDefs
{
	TArray<FString> GroupNames;
	TArray<FMarkerCurveDef> CurveDefs;
	TMap<FString, int32> Landmarks;
	TMap<FString, FVector2D> DefaultScreenPoints;
	TMap<FString, FString> CurveMeshesForMarkers;
};

class FLandmarkConfigIdentityHelper
{
public:

	UE_API FLandmarkConfigIdentityHelper();
	UE_API ~FLandmarkConfigIdentityHelper();

	/** Returns all the marker definitions as per config */
	UE_API TSharedPtr<struct FMarkerDefs> GetMarkerDefs() const;

	UE_API TArray<FString> GetGroupListForSelectedPreset(const ECurvePresetType& InSelectedPose) const;

	/** Projects the 2D points based on 3D position of vertex IDs of the archetype mesh */
	UE_API FFrameTrackingContourData ProjectPromotedFrameCurvesOnTemplateMesh(const struct FMinimalViewInfo& InViewInfo, 
		const TMap<EIdentityPartMeshes, TArray<FVector>>& InTemplateMeshVertices, const ECurvePresetType& InSelectedPreset, const FIntRect& InViewRect) const;

	/** Uses preset values for curves in the config */
	UE_API FFrameTrackingContourData GetDefaultContourDataFromConfig(const FVector2D& InTexResolution, const ECurvePresetType& InSelectedPreset) const;

	/** Convert Identity Pose Type enum into a curve preset type */
	UE_API ECurvePresetType GetCurvePresetFromIdentityPose(const EIdentityPoseType& InIdentityPoseType) const;

private:

	UE_API void GetProjectedScreenCoordinates(const TArray<FVector>& InWorldPositions, const struct FMinimalViewInfo& InViewInfo,
		TArray<FVector2d>& OutScreenPositions, const FIntRect& InViewRect) const;

	UE_API void PopulateMarkerDataFromConfig(const TMap<FString, TSharedPtr<class FJsonValue>>& InConfigContourData);

	UE_API bool LoadCurvesAndLandmarksFromJson(const FString& InFileName);
	UE_API bool LoadGroupsFromJson(const FString& InFileName) const;

	UE_API TArray<struct FMarkerCurveDef> GetCurvesForPreset(const ECurvePresetType& InSelectedPose) const;

	UE_API EIdentityPartMeshes GetMeshPartFromConfigName(const FString& InMeshName) const;

	/** A struct containing non-changing marker group and curve data */
	TSharedPtr<struct FMarkerDefs> MarkerDefs;

	TSet<FString> NeutralPoseCurveExclusionList;
	TSet<FString> NeutralPoseGroupExclusionList;

	TSet<FString> TeethPoseCurveExclusionList;
	TSet<FString> TeethPoseGroupExclusionList;

	TSet<FString> PerformanceCurveList;
	TSet<FString> PerformanceCurveGroups;

	UE_API const static FString ConfigGroupFileName;
};

#undef UE_API

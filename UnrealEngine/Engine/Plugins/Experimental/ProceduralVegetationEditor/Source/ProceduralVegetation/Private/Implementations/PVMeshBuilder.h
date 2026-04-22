// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PVFloatRamp.h"
#include "PVMaterialSettings.h"
#include "UDynamicMesh.h"

#include "Curves/CurveFloat.h"

#include "Engine/Texture2D.h"

#include "Facades/PVBranchFacade.h"
#include "Facades/PVPlantFacade.h"
#include "Facades/PVPointFacade.h"

#include "Generators/MeshShapeGenerator.h"

#include "GeometryCollection/GeometryCollection.h"
#include "GeometryCollection/ManagedArrayCollection.h"

#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInterface.h"

#include "PVMeshBuilder.generated.h"

USTRUCT()
struct FPVMeshBuilderParams
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category="Materials",
		meta=(PinHiddenByDefault, InlineEditConditionToggle, Tooltip=
			"Toggles between default and custom material settings.\n\nDefault material setting is defined in Procedural Vegetation Preset.\n\nCustom material settings allow for editing."
		))
	bool bOverrideMaterial = false;

	UPROPERTY(EditAnywhere, Category="Materials", meta = (EditCondition="bOverrideMaterial"))
	FPVMaterialSettings MaterialSettings;

	UPROPERTY(EditAnywhere, Category="Mesh Settings", DisplayName="Point Removal",
		meta=(ClampMin=0.0f, ClampMax=0.1f, UIMin=0.0f, UIMax=0.1f, Tooltip=
			"Removes small points to simplify the mesh.\n\nPoints are used to define the tree structure and used as basis for generating the mesh. This does not remove points but reduces the points considered for mesh generation. The impact is dependent on Hull, Main Trunk, Ground and Scale Retention settings."
		))
	float PointRemoval = 0.0f;

	UPROPERTY(EditAnywhere, Category="Mesh Settings", DisplayName="Segment Reduction",
		meta=(ClampMin=0.0f, ClampMax=1.0f, UIMin=0.0f, UIMax=1.0f, Tooltip=
			"Controls how aggressively segments are reduced.\n\nSegments are created based on the number of points required to represent the shape of the plant. The algorithm tries to simplify the polyline by reducing the number of points required to represent it, while maintaining its overall shape. The impact is dependent on Hull, Main Trunk, Ground and Scale Retention settings.\nHigher values reduce edge count for lighter meshes; lower values preserve detail."
		))
	float SegmentReduction = 0.0f;

	UPROPERTY(EditAnywhere, Category="Mesh Settings", DisplayName="Min Divisions",
		meta=(ClampMin=3, ClampMax=1024, UIMin=3, UIMax=36, Tooltip=
			"Minimum radial divisions.\n\nThe lowest polygon count around cross-sections. Higher values keep sections smooth; lower values produce blockier geometry with better performance."
		))
	int32 MinMeshDivisions = 6;

	UPROPERTY(EditAnywhere, Category="Mesh Settings", DisplayName="Max Divisions",
		meta=(ClampMin=3, ClampMax=1024, UIMin=3, UIMax=36, Tooltip=
			"Maximum radial divisions.\n\nUpper limit for cross-section detail on trunks/branches. Balance with Min Divisions to control density across sizes."
		))
	int32 MaxMeshDivisions = 12;

	UPROPERTY(EditAnywhere, Category="Mesh Settings|Advanced", DisplayName="Reduction Accuracy",
		meta=(ClampMin=1, ClampMax=15, UIMin=1, UIMax=15, Tooltip=
			"Accuracy level for mesh reduction.\n\nControls how closely the simplified mesh follows the original. It specifies number of iterations that are used to evaluate the curve reduction path. The more number of iterations the more accurate the reduction will be at the cost of processing power."
		))
	int32 Accuracy = 5;

	UPROPERTY(EditAnywhere, Category="Mesh Settings|Advanced", DisplayName="Longest Segment Allowed (meters)",
		meta=(ClampMin=0.0f, ClampMax=50.0f, UIMin=0.0f, UIMax=10.0f, Tooltip=
			"Maximum allowed segment length.\n\nCaps segment length after simplification to maintain even geometry and prevent stretched polygons."
		))
	float LongestSegmentLength = 10.0f;

	UPROPERTY(EditAnywhere, Category="Mesh Settings|Advanced", DisplayName="Shortest Segment Allowed (meters)",
		meta=(ClampMin=0.0f, ClampMax=50.0f, UIMin=0.0f, UIMax=1.0f, Tooltip=
			"Minimum allowed segment length.\n\nSegments shorter than this are merged or removed to avoid unnecessary fine detail that inflates polycount."
		))
	float ShortestSegmentLength = 0.0f;

	UPROPERTY(EditAnywhere, Category="Mesh Settings|Advanced", DisplayName="Segment Retention Impact",
		meta=(ClampMin=0.0f, ClampMax=5.0f, UIMin=0.0f, UIMax=1.0f, Tooltip=
			"Control the impact of reduced segments based on other criteria\n\nSegment retention impact allows you to reintroduce points that have been reduced based on user defined criteria\'s. See; Hull Retention, Ground Retention and Trunk Retention."
		))
	float SegmentRetentionImpact = 1.0f;

	UPROPERTY(EditAnywhere, Category="Mesh Settings|Advanced", DisplayName="Hull Retention",
		meta=(ClampMin=0.0f, ClampMax=1.0f, UIMin=0.0f, UIMax=1.0f, Tooltip=
			"Strength of preserving the outer hull.\n\nAdjusts retention of the mesh silhouette during reduction. Higher values protect external shape; lower values allow more simplification."
		))
	float HullRetention = 0.0f;

	UPROPERTY(EditAnywhere, Category="Mesh Settings|Advanced", DisplayName="Main Trunk Retention",
		meta=(ClampMin=0.0f, ClampMax=1.0f, UIMin=0.0f, UIMax=1.0f, Tooltip=
			"Preserves detail in the main trunk.\n\nControls how much trunk geometry is retained during simplification. High values keep trunk smooth and detailed; low values reduce complexity."
		))
	float MainTrunkRetention = 0.0f;

	UPROPERTY(EditAnywhere, Category="Mesh Settings|Advanced", DisplayName="Ground Retention",
		meta=(ClampMin=0.0f, ClampMax=1.0f, UIMin=0.0f, UIMax=1.0f, Tooltip=
			"Preserves detail near ground regions.\n\nAdjusts retention for areas close to the base of the plant. Use higher values to keep root/ground transitions detailed."
		))
	float GroundRetention = 0.0f;

	UPROPERTY(EditAnywhere, Category="Mesh Settings|Advanced", DisplayName="Hull Retention Gradient",
		meta=(XAxisMin=0.0f, XAxisMax=1.0f, YAxisMin=0.0f, YAxisMax=1.0f, Tooltip=
			"Gradient for preserving outer hull detail.\n\nA ramp that varies outer-hull retention across the model. Use to keep silhouettes crisp where needed while simplifying elsewhere."
		))
	FPVFloatRamp HullRetentionGradient;

	UPROPERTY(EditAnywhere, Category="Mesh Settings|Advanced", DisplayName="Main Trunk Retention Gradient",
		meta=(XAxisMin=0.0f, XAxisMax=1.0f, YAxisMin=0.0f, YAxisMax=1.0f, Tooltip=
			"Gradient controlling trunk detail retention.\n\nVaries trunk retention via a ramp. Useful to preserve key trunk regions while simplifying others."
		))
	FPVFloatRamp MainTrunkRetentionGradient;

	UPROPERTY(EditAnywhere, Category="Mesh Settings|Advanced", DisplayName="Ground Retention Gradient",
		meta=(XAxisMin=0.0f, XAxisMax=1.0f, YAxisMin=0.0f, YAxisMax=1.0f, Tooltip=
			"Gradient for preserving ground-region detail.\n\nA ramp that modulates retention around base regions. Tune to keep roots/basal features while allowing simplification above."
		))
	FPVFloatRamp GroundRetentionGradient;

	UPROPERTY(EditAnywhere, Category="Mesh Settings|Advanced", DisplayName="Scale Retention",
		meta=(ClampMin=0.0f, ClampMax=1.0f, UIMin=0.0f, UIMax=1.0f, Tooltip=
			"Preserves detail relative to branch size.\n\nRetains more detail on larger/thicker structures and simplifies smaller ones to prioritize visually important geometry."
		))
	float ScaleRetention = 0.0f;

	UPROPERTY(EditAnywhere, Category="Mesh Settings|Advanced", DisplayName="Scale Retention Gradient",
		meta=(XAxisMin=0.0f, XAxisMax=1.0f, YAxisMin=0.0f, YAxisMax=1.0f, Tooltip=
			"Gradient controlling size-based retention.\n\nUses a ramp to vary how size-dependent retention is applied across the model. Keep large structures crisp while simplifying finer parts."
		))
	FPVFloatRamp ScaleRetentionGradient;

	UPROPERTY(EditAnywhere, Category="Plant Profile Settings", DisplayName="Profile",
		meta=(EditCondition="bIsPlantProfileDropdownEnabled", EditConditionHides, HideEditConditionToggle, GetOptions="GetPlantProfileOptions",
			Tooltip="Select a profile (cross sectional shape) from predefined options\n\nNone applies a simple circular cross sectional shape"))
	FString SelectedPlantProfile = "None";

	UPROPERTY(Transient)
	TArray<FString> PlantProfileOptions = {"None"};

	UPROPERTY(Transient)
	bool bIsPlantProfileDropdownEnabled = false;

	UPROPERTY(EditAnywhere, Category="Plant Profile Settings", DisplayName="Profile Falloff",
		meta=(XAxisMin=0.0f, XAxisMax=1.0f, YAxisMin=0.0f, YAxisMax=1.0f, EditCondition="bIsPlantProfileDropdownEnabled", EditConditionHides,
			HideEditConditionToggle, Tooltip=
			"Gradient controlling how the profile shape is applied across the length of the trunk and branches\n\nCan be used to create a base where the trunk widens and transitions into main roots (Root Flare)"
		))
	FPVFloatRamp PlantProfileFallOff;

	UPROPERTY(EditAnywhere, Category="Plant Profile Settings", DisplayName="Profile Scale",
		meta=(ClampMin=0.0f, ClampMax=10.0f, UIMin=0.0f, UIMax=10.0f, EditCondition="bIsPlantProfileDropdownEnabled", EditConditionHides,
			HideEditConditionToggle, Tooltip="Scale of the profile applied\n\nCan be used to create a wider base in conjection with Profile Falloff"))
	float PlantProfileScale = 1.0f;

	UPROPERTY(EditAnywhere, Category="Plant Profile Settings",
		meta=(EditCondition="bIsPlantProfileDropdownEnabled", EditConditionHides, HideEditConditionToggle, Tooltip=
			"Should the profile be applied to branches or just the trunk"))
	bool bApplyProfileToBranches = false;

	UPROPERTY(EditAnywhere, Category = "Displacement Settings",
		meta=(Tooltip=
			"Texture used to drive mesh displacement.\n\nAssigns the texture map that controls displacement. Bright and dark areas in the texture push or pull the mesh surface to create additional geometric detail such as bark roughness, grooves, or ridges.\n\nOnly power of 2 textures with source texture formats (TSF_RGBA32F, TSF_RGBA16F, TSF_BGRA8, TSF_R32F, TSF_R16F, TSF_G8) are supported.\n\nFor formats TSF_RGBA32F, TSF_RGBA16F and TSF_BGRA8 data from R channel is used."
		))
	TObjectPtr<UTexture2D> DisplacementTexture = nullptr;

	UPROPERTY(Transient, NonTransactional)
	TArray<float> DisplacementValues;

	UPROPERTY(EditAnywhere, Category="Displacement Settings", DisplayName="Displacement Scale",
		meta=(ClampMin=0.0f, ClampMax=10.0f, UIMin=0.0f, UIMax=1.0f, Tooltip=
			"Intensity of displacement effect.\n\nSets the overall strength of the displacement. Higher values exaggerate surface depth and detail, while lower values apply subtle variation. Use to balance realism with mesh stability."
		))
	float DisplacementStrength = 0.5f;

	UPROPERTY(EditAnywhere, Category="Displacement Settings", DisplayName="Displacement Bias",
		meta=(ClampMin=0.0f, ClampMax=10.0f, UIMin=0.0f, UIMax=10.0f, Tooltip=
			"Shifts the midpoint of displacement.\n\nOffsets the neutral level of the displacement map. Positive bias lifts the surface outward. Useful for correcting textures that displace unevenly or need centering."
		))
	float DisplacementBias = 0.0f;

	UPROPERTY(EditAnywhere, Category="Displacement Settings", DisplayName="Displacement UV Scale",
		meta=(Tooltip=
			"Controls tiling of displacement texture.\n\nAdjusts the repetition of the displacement texture across the mesh in U and V directions. Higher values increase tiling frequency (smaller details repeated more often), while lower values stretch the texture over a larger area."
		))
	FVector2f DisplacementUVScale = FVector2f(1.0f, 1.0f);

	UPROPERTY(EditAnywhere, Category="Displacement Settings", DisplayName="Apply Displacement up to generation",
		meta=(ClampMin=1, ClampMax=10, UIMin=1, UIMax=10, Tooltip=
			"Controls which generations the displacement is applied on \n\nControls which generations the displacement is applied on."))
	int32 DisplacementGenerationUpperLimit = 1;

	FPVMeshBuilderParams()
	{
		InitializeLinearCurve(HullRetentionGradient);
		InitializeLinearCurve(MainTrunkRetentionGradient);
		InitializeLinearCurve(GroundRetentionGradient);
		InitializeLinearCurve(ScaleRetentionGradient);
		InitializeLinearCurve(PlantProfileFallOff);
	}

private:
	static void InitializeLinearCurve(FPVFloatRamp& Curve)
	{
		if (FRichCurve* RichCurve = Curve.GetRichCurve())
		{
			RichCurve->Reset();
			const FKeyHandle Handle0 = RichCurve->AddKey(0.0f, 1.0f);
			const FKeyHandle Handle1 = RichCurve->AddKey(1.0f, 0.0f);

			RichCurve->SetKeyInterpMode(Handle0, RCIM_Linear);
			RichCurve->SetKeyInterpMode(Handle1, RCIM_Linear);
		}
	}
};

struct FLocalDynamicMeshData
{
	struct FVertex
	{
		FVector3f Position;
		FVector3f Normal;
		FVector2f UV;
		FVector2f UV1;
		FVector2f UV2;
		float NjordPixelId;
	};

	TArray<FVertex> Vertices;
	TArray<FIntVector4> Triangles;
	int MaterialID = 0;
};

struct FDisplacementData
{
	const TArrayView<const float> Values;
	const int32 TextureWidth;
	const int32 TextureHeight;

	FDisplacementData(const TArrayView<const float>& InValues, const int32 InWidth, const int32 InHeight)
		: Values(InValues)
		, TextureWidth(InWidth)
		, TextureHeight(InHeight)
	{
	}
};

class FPVMeshGenerator : public UE::Geometry::FMeshShapeGenerator
{
	int32 VerticesCount = 0;
	int32 TriangleCount = 0;

	TArray<FLocalDynamicMeshData>* MeshesDatas = nullptr;

public:
	FPVMeshGenerator(const int32 InVerticesCount, const int32 InTriangleCount, TArray<FLocalDynamicMeshData>* InMeshesDatas)
		: VerticesCount(InVerticesCount)
		, TriangleCount(InTriangleCount)
		, MeshesDatas(InMeshesDatas)
	{
	}

	virtual FMeshShapeGenerator& Generate() override
	{
		SetBufferSizes(VerticesCount, TriangleCount, VerticesCount, TriangleCount);

		int32 VertexIndex = 0;
		int32 FaceIndex = 0;
		for (auto MeshIndex = MeshesDatas->Num() - 1; MeshIndex >= 0; --MeshIndex)
		{
			const FLocalDynamicMeshData& MeshesData = (*MeshesDatas)[MeshIndex];
			for (const FIntVector4& Triangle : MeshesData.Triangles)
			{
				const FIntVector4 OffsetTriangle = Triangle + FIntVector4(VertexIndex);
				const UE::Geometry::FIndex3i Indices = FIntVector(OffsetTriangle);
				Triangles[FaceIndex] = Indices;
				TriangleUVs[FaceIndex] = Indices;
				TriangleNormals[FaceIndex] = Indices;
				TrianglePolygonIDs[FaceIndex] = OffsetTriangle.W;
				++FaceIndex;
			}
			for (const FLocalDynamicMeshData::FVertex& Vertex : MeshesData.Vertices)
			{
				Vertices[VertexIndex] = static_cast<FVector>(Vertex.Position);
				Normals[VertexIndex] = Vertex.Normal;
				NormalParentVertex[VertexIndex] = VertexIndex;
				UVs[VertexIndex] = Vertex.UV;
				UVParentVertex[VertexIndex] = VertexIndex;
				++VertexIndex;
			}
		}

		return *this;
	}
};

struct FPVMeshBuilder
{
	static void GenerateGeometryCollection(const FManagedArrayCollection& InSkeletonCollection, const FPVMeshBuilderParams& MeshBuilderParams,
	                                       FGeometryCollection& OutGeometryCollection);

	static void SetNjordPixelID(FGeometryCollection& OutGeometryCollection);

	static void GenerateDynamicMesh(FManagedArrayCollection& Collection, const FPVMeshBuilderParams& MeshBuilderParams,
	                                TObjectPtr<UDynamicMesh>& OutMesh);

	static bool ExtractDisplacementData(const TObjectPtr<UTexture2D>& Texture, TArray<float>& OutValues, FString& OutError);

private:
	static TSet<int32> CollectHardPoints(const PV::Facades::FBranchFacade& BranchFacade, const PV::Facades::FPointFacade& PointFacade,
	                                     const PV::Facades::FPlantFacade& PlantFacade);

	static TSet<int32> ComputePointGradients(const PV::Facades::FPointFacade& PointFacade, const FPVMeshBuilderParams& MeshBuilderParams,
	                                         const TSet<int32>& HardPoints, const float MaxPointScale, TArray<float>& OutMeshDivisionsGradients,
	                                         TArray<float>& OutDeltaModifiers);

	static float GetMaxDeltaBetweenHardPoints(const PV::Facades::FBranchFacade& BranchFacade, const PV::Facades::FPointFacade& PointFacade,
	                                          const TSet<int32>& HardPoints);

	static void PerformPathSimplification(const PV::Facades::FBranchFacade& BranchFacade,
	                                      const PV::Facades::FPointFacade& PointFacade, const FPVMeshBuilderParams& MeshBuilderParams,
	                                      const float MaxPointScale, const TSet<int32>& HardPoints, const TArray<float>& DeltaModifiers,
	                                      TSet<int32>& InOutPointsToRemove);

	static TArray<int32> ComputeMeshDivisions(const PV::Facades::FBranchFacade& BranchFacade, const FPVMeshBuilderParams& MeshBuilderParams,
	                                          const TArray<float>& MeshDivisionsGradients, const int32 PointCount);

	static void TriangulateRings(const TArray<int32>& PreviousIndices, const TArray<int32>& CurrentIndices, int32& InOutPolyGroupIndex,
	                             FLocalDynamicMeshData& OutMeshData);

	static float GetProfileMultiplier(const TArray<float>& InProfilePoints, const float ProfileUV_U);

	static TMap<int32, TArray<int32>> GetPointsIndicesToFoliageIndicesMap(const FManagedArrayCollection& Collection);

	static void GenerateBranchMeshData(const bool bPrimitiveIsTrunk, const int32 GenerationNumber, const TArray<int32>& PrimitivePoints,
	                                   const FDisplacementData& DisplacementData, const FPVMeshBuilderParams& MeshBuilderParams,
	                                   const TArray<int32>& TargetMeshDivisions, const FManagedArrayCollection& Collection,
	                                   FLocalDynamicMeshData& OutLocalMeshData);

	static void UpdateFoliagePivotPoints(const TSet<int32>& PointsToRemove, FManagedArrayCollection& OutCollection);

	static void GetLocalDynamicMeshData(FManagedArrayCollection& Collection, const FPVMeshBuilderParams& MeshBuilderParams,
	                                    TArray<FLocalDynamicMeshData>& MeshesData);
};

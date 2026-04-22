// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

namespace PV::GroupNames
{
	//Group Names
	inline static const FName BranchGroup = FName(TEXT("Primitives"));
	inline static const FName PointGroup = FName(TEXT("Points"));
	inline static const FName FoliageGroup = FName(TEXT("Foliage"));
	inline static const FName FoliageNamesGroup = FName(TEXT("FoliageNames"));
	inline static const FName DetailsGroup = FName(TEXT("Details"));
	inline static const FName PlantProfilesGroup = FName(TEXT("PlantProfiles"));
	inline static const FName BonesGroup = FName(TEXT("Bones"));
	inline static const FName VerticesGroup = FName(TEXT("Vertices"));
}

namespace PV::AttributeNames
{
	//Branch Attributes
	inline static const FName BranchParents = FName(TEXT("parents"));
	inline static const FName BranchChildren = FName(TEXT("children"));
	inline static const FName BranchPoints = FName(TEXT("Points"));
	inline static const FName BranchNumber = FName(TEXT("branchNumber"));
	inline static const FName BranchSourceBudNumber = FName(TEXT("branchSourceBudNumber"));
	inline static const FName BranchFoliageIDs = FName(TEXT("FoliageID"));
	inline static const FName BranchUVMaterial = FName(TEXT("branchUVMat"));
	inline static const FName TrunkMaterialPath = FName(TEXT("TrunkMaterialPath"));
	inline static const FName TrunkURange = FName(TEXT("TrunkURange"));
	inline static const FName BranchHierarchyNumber = FName(TEXT("branchHierarchyNumber"));
	inline static const FName BranchSimulationGroupIndex = FName(TEXT("branchSimulationGroupIndex"));
	inline static const FName PlantNumber = FName(TEXT("plantNumber"));
	inline static const FName BranchParentNumber = FName(TEXT("branchParentNumber"));

	//Point Attributes
	inline static const FName PointPosition = FName(TEXT("Position"));
	inline static const FName LengthFromRoot = FName(TEXT("lengthFromRoot"));
	inline static const FName PointScaleGradient = FName(TEXT("LOD_totalPscaleGradient"));
	inline static const FName PointScale = FName(TEXT("Scale"));
	
	inline static const FName LengthFromSeed = FName(TEXT("lengthFromSeed"));
	inline static const FName BudLightDetected = FName(TEXT("budLightDetected"));
	inline static const FName BudDevelopment = FName(TEXT("budDevelopment"));
	inline static const FName TextureCoordV = FName(TEXT("uv_v"));
	inline static const FName TextureCoordUOffset = FName(TEXT("uv_uOffset"));
	inline static const FName URange = FName(TEXT("uv_uRange"));
	
	inline static const FName HullGradient = FName(TEXT("LOD_hullGradient"));
	inline static const FName MainTrunkGradient = FName(TEXT("LOD_mainTrunkGradient"));
	inline static const FName GroundGradient = FName(TEXT("LOD_groundGradient"));
	inline static const FName BudNumber = FName(TEXT("budNumber"));
	inline static const FName BudHormoneLevels = FName(TEXT("budHormoneLevels"));
	inline static const FName BudDirection = FName(TEXT("budDirection"));
	inline static const FName BranchGradient = FName(TEXT("branchGradient"));
	inline static const FName PlantGradient = FName(TEXT("plantGradient"));
	inline static const FName NjordPixelIndex = FName(TEXT("njord_pixelIdx"));

	//Foliage Attributes
	inline static const FName FoliageNameID = FName(TEXT("FoliageNameID"));
	inline static const FName FoliageBranchID = FName(TEXT("FoliageBranchID"));
	inline static const FName FoliagePivotPoint = FName(TEXT("FoliagePivotPoint"));
	inline static const FName FoliageUPVector = FName(TEXT("FoliageUPVector"));
	inline static const FName FoliageNormalVector = FName(TEXT("FoliageNormalVector"));
	inline static const FName FoliageScale = FName(TEXT("FoliageScale"));
	inline static const FName FoliageLengthFromRoot = FName(TEXT("LengthFromRoot"));
	inline static const FName FoliageParentBoneID = FName(TEXT("FoliageParentBoneID"));

	//FoliageNames Attributes
	inline static const FName FoliageName = FName(TEXT("Name"));

	//Details Attributes
	inline static const FName LeafPhyllotaxy = FName(TEXT("phyllotaxyLeaf"));
	inline static const FName FoliagePath = FName(TEXT("FoliagePath"));
	inline static const FName Guid = FName(TEXT("Guid"));

	//PlantProfiles Attributes
	inline static const FName ProfilePoints = FName(TEXT("ProfilePoints"));

	//Bone Attributes
	inline static const FName BoneName = FName(TEXT("BoneName"));
	inline static const FName BoneParentIndex = FName(TEXT("BoneParentIndex"));
	inline static const FName BonePose = FName(TEXT("BonePose"));
	inline static const FName BonePointIndex = FName(TEXT("BonePointIndex"));
	inline static const FName BoneAbsolutePosition = FName(TEXT("BoneAbsolutePosition"));
	inline static const FName BoneBranchIndex = FName(TEXT("BoneBranchIndex"));
	inline static const FName NjordPixelId = FName(TEXT("NjordPixelId"));
	inline static const FName VertexPointIds = FName(TEXT("VertexPointIds"));
	inline static const FName BoneId = FName(TEXT("BoneId"));
}


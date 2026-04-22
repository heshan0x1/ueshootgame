// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MetaHumanCharacterGeneratedAssets.h"
#include "MetaHumanGeometryRemovalTypes.h"
#include "MetaHumanPaletteItemKey.h"

#include "MetaHumanCharacterPipelineData.generated.h"

class UMaterialInterface;
class USkeletalMesh;
class UTexture;
class UTexture2D;

/** Transient data for a Character used during assembly */
USTRUCT()
struct FCharacterPipelineData
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<USkeletalMesh> FaceMesh;
	UPROPERTY()
	TObjectPtr<USkeletalMesh> BodyMesh;

	UPROPERTY()
	FMetaHumanCharacterGeneratedAssets GeneratedAssets;
	bool bIsGeneratedAssetsValid = false;

	UPROPERTY()
	TMap<FName, TObjectPtr<UMaterialInterface>> FaceRemovedMaterialSlots;

	UPROPERTY()
	TArray<TObjectPtr<UTexture2D>> FaceBakedNormalsTextures;

	UPROPERTY()
	TObjectPtr<UTexture2D> FollicleMap;

	UPROPERTY()
	TArray<TObjectPtr<UTexture>> PreBakedGroomTextures;

	UPROPERTY()
	TArray<UE::MetaHuman::GeometryRemoval::FHiddenFaceMapTexture> HeadHiddenFaceMaps;
	UPROPERTY()
	TArray<UE::MetaHuman::GeometryRemoval::FHiddenFaceMapTexture> BodyHiddenFaceMaps;
	
	// Each time a material parameter (or set of material parameters) is changed on a face mesh LOD,
	// the entry of the index of that LOD should be incremented in this array.
	//
	// It will be used to determine which face LODs have unique materials and need to be baked 
	// separately.
	TArray<int32> FaceMaterialChangesPerLOD;

	UPROPERTY()
	TObjectPtr<USkeletalMesh> MergedHeadAndBody;

	bool bTransferSkinWeights = true;
	bool bStripSimMesh = false;
};

/** 
 * A containing object for FCharacterPipelineData, so that temporary objects it references are 
 * visible to GC.
 */
UCLASS(Transient)
class UCharacterPipelineDataMap : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY()
	TMap<FMetaHumanPaletteItemKey, FCharacterPipelineData> Map;
};

// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/DataAsset.h"

#include "GeometryCollection/ManagedArrayCollection.h"

#include "ProceduralVegetationPreset.generated.h"

USTRUCT()
struct FPVPresetVariationInfo
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category = "Variations")
	FName Name;

	UPROPERTY(VisibleAnywhere, Category = "Variations")
	TArray<TSoftObjectPtr<UObject>> FoliageMeshes;

	UPROPERTY(VisibleAnywhere, Category = "Variations")
	TArray<TSoftObjectPtr<UObject>> Materials;

	UPROPERTY(VisibleAnywhere, Category = "Variations")
	TArray<FString> PlantProfiles;

	void Fill(FName InName,FManagedArrayCollection Collection);
};

UCLASS(BlueprintType, hidecategories="Internal")
class PROCEDURALVEGETATION_API UProceduralVegetationPreset : public UDataAsset
{
	GENERATED_BODY()

public:
	UProceduralVegetationPreset();

	virtual void PostLoad() override;

	UPROPERTY(EditAnywhere, Category="Internal", meta = (DevelopmentOnly))
	FDirectoryPath JsonDirectoryPath;
	
	UPROPERTY(EditAnywhere, Category = "Internal", Meta = (ContentDir, DevelopmentOnly))
	bool bOverrideFolderPaths = false;
	
	UPROPERTY(EditAnywhere, Category = "Internal", Meta = (ContentDir, DevelopmentOnly , EditCondition = "bOverrideFolderPaths"))
	FDirectoryPath FoliageFolder;
	
	UPROPERTY(EditAnywhere, Category = "Internal", Meta = (ContentDir, DevelopmentOnly , EditCondition = "bOverrideFolderPaths"))
	FDirectoryPath MaterialsFolder;
	
	UPROPERTY(EditAnywhere, Category = "Internal", meta = (DevelopmentOnly))
	FString TrunkMaterialName;
	
	UPROPERTY(EditAnywhere, Category = "Internal", meta = (DevelopmentOnly))
	bool bCreateProfileDataAsset = false;
	
	UPROPERTY(EditAnywhere, Category = "Internal", meta = (DevelopmentOnly, EditCondition = "bCreateProfileDataAsset"))
	FString PlantProfileName;
	
	UPROPERTY()
	TMap<FString, FManagedArrayCollection> Variants;

	UPROPERTY(VisibleAnywhere, Category = "Preset Data")
	TArray<FPVPresetVariationInfo> PresetVariations;

public:
	void LoadFromVariantFiles(const TMap<FString, FString>& InVariantFiles);

	UFUNCTION(CallInEditor, Category="Internal", meta = (DevelopmentOnly))
	void UpdateDataAsset();

	void FillVariationInfo();

	static void ShowHideInternalProperties(bool bDebugEnable);
private:
	void CreateProfileDataAsset();
	
	void UpdateFoliageAndMaterialPath();
};

USTRUCT(BlueprintType)
struct PROCEDURALVEGETATION_API FPlantProfile
{
	GENERATED_BODY()
public:
	UPROPERTY(VisibleAnywhere, Category="PlantProfile")
	FString ProfileName;
	UPROPERTY(VisibleAnywhere, Category="PlantProfile")
	TArray<float> ProfilePoints;
};

UCLASS(BlueprintType)
class PROCEDURALVEGETATION_API UPlantProfileAsset : public UDataAsset
{
	GENERATED_BODY()
public:
	UPROPERTY(VisibleAnywhere, Category="PlantProfile")
	TArray<FPlantProfile> Profiles;
};
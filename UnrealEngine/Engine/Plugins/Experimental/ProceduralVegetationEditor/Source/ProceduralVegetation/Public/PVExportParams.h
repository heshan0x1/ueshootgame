// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Engine/EngineTypes.h"
#include "PVWindSettings.h"

#include "Misc/Paths.h"
#include "PVExportParams.generated.h"

UENUM()
enum class EPVExportMeshType: uint8
{
	StaticMesh,
	SkeletalMesh,
};

UENUM()
enum class EPVAssetReplacePolicy : uint8
{
	/** Create new assets with numbered suffixes */
	Append,

	/** Replaces existing asset with new asset */
	Replace,

	/** Ignores the new asset and keeps the existing asset */
	Ignore,
};

UENUM()
enum class EPVCollisionGeneration : uint8
{
	/** Do not create collision for mesh */
	None,

	/** Creates collision for trunk only */
	TrunkOnly,

	/** Creates collision for all branch generations */
	AllGenerations,
};

USTRUCT()
struct PROCEDURALVEGETATION_API FPVExportParams
{
	GENERATED_BODY()

	UPROPERTY()
	bool bShouldExport = true;

	UPROPERTY()
	bool bShouldExportFoliage = true;

	UPROPERTY(EditAnywhere, Category = "Asset", Meta = (ContentDir, DisplayName = "Content Browser Folder", Tooltip="Path to the folder where exported mesh will be saved\n\nPath to the folder where exported mesh will be saved."))
	FDirectoryPath ContentBrowserFolder;

	UPROPERTY(EditAnywhere, Category = "Asset", Meta = (DisplayName = "Mesh Name", Tooltip="Name of the exported mesh\n\nName of the exported mesh."))
	FName MeshName;

	UPROPERTY(EditAnywhere, Category = "Asset", Meta = (DisplayName = "Asset Replace Policy", Tooltip="Export behaviour if asset already exists."))
	EPVAssetReplacePolicy ReplacePolicy = EPVAssetReplacePolicy::Replace;

	UPROPERTY(EditAnywhere, Category = "Mesh", Meta = (DisplayName = "Export Mesh Type", Tooltip="Static Mesh or Skeletal Mesh."))
	EPVExportMeshType ExportMeshType = EPVExportMeshType::SkeletalMesh;

	UPROPERTY(EditAnywhere, Category = "Mesh | Nanite", Meta = (DisplayName = "Create Nanite Foliage", Tooltip="Add Nanite foliage support for the exported mesh."))
	bool bCreateNaniteFoliage = true;
	
	UPROPERTY(EditAnywhere, Category = "Mesh | Nanite", Meta = (DisplayName = "Nanite Shape Preservation Method", Tooltip="Shape preservation setting to be used in nanite settings."))
	ENaniteShapePreservation NaniteShapePreservation = ENaniteShapePreservation::Voxelize;

	UPROPERTY(EditAnywhere, Category = "Mesh | Collision", Meta = (EditCondition = "ExportMeshType == EPVExportMeshType::StaticMesh", EditConditionHides, Tooltip="Creates the collision for static mesh."))
	bool bCollision = false;

	UPROPERTY(EditAnywhere, Category = "Mesh | Collision", Meta = (EditCondition = "ExportMeshType == EPVExportMeshType::SkeletalMesh", EditConditionHides, Tooltip="Creates the physics asset for skeletal mesh."))
	EPVCollisionGeneration CollisionGeneration = EPVCollisionGeneration::None;
	
	UPROPERTY(EditAnywhere, Category = "Mesh | Dynamic Wind", Meta = (EditCondition = "ExportMeshType == EPVExportMeshType::SkeletalMesh", EditConditionHides, Tooltip="Wind Settings contains the wind simulation group data, Default presets are available for Saplings and Trees."))
	TObjectPtr<UPVWindSettings> WindSettings;

	void Initialize(const FString& InAssetPath, const FString& InName, const FString& InDefaultWindSettingsPath)
	{
		ContentBrowserFolder.Path = FPaths::GetPath(InAssetPath);
		MeshName = FName(InName);
		ReplacePolicy = EPVAssetReplacePolicy::Replace;

		WindSettings = LoadObject<UPVWindSettings>(nullptr, InDefaultWindSettingsPath);
	}

	FString GetOutputObjectPath() const
	{
		const FString AssetName = MeshName.ToString();
		const FString OutputPath = ContentBrowserFolder.Path / AssetName + '.' + AssetName;
		return OutputPath;
	}

	FString GetOutputMeshPackagePath() const
	{
		return FPaths::Combine(ContentBrowserFolder.Path, MeshName.ToString());
	}

	FString GetOutputSkeletonName() const
	{
		return FString::Printf(TEXT("%s_Skeleton"), *MeshName.ToString());
	}

	FString GetOutputSkeletonPackagePath() const
	{
		return FPaths::Combine(ContentBrowserFolder.Path, GetOutputSkeletonName());
	}

	FString GetOutputPhysicsAssetName() const
	{
		return FString::Printf(TEXT("%s_Physics"), *MeshName.ToString());
	}

	FString GetOutputPhysicsAssetPackagePath() const
	{
		return FPaths::Combine(ContentBrowserFolder.Path, GetOutputPhysicsAssetName());
	}
	
	bool IsCollisionEnable() const
	{
		return CollisionGeneration != EPVCollisionGeneration::None;
	}

	UClass* GetMeshClass() const;

	bool Validate(FString& OutError) const;
};
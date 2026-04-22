// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "RigVMBlueprintGeneratedClass.h"
#include "Rigs/RigModuleDefines.h"
#include "ControlRigBlueprintGeneratedClass.generated.h"

#define UE_API CONTROLRIG_API

UCLASS(MinimalAPI)
class UControlRigBlueprintGeneratedClass : public URigVMBlueprintGeneratedClass
{
	GENERATED_UCLASS_BODY()

public:

	// UObject interface
	UE_API void Serialize(FArchive& Ar);

	UPROPERTY(AssetRegistrySearchable)
	FSoftObjectPath PreviewSkeletalMesh;

	UPROPERTY(AssetRegistrySearchable)
	bool bExposesAnimatableControls;

	UPROPERTY()
	bool bAllowMultipleInstances;

	UPROPERTY(AssetRegistrySearchable)
	EControlRigType ControlRigType;

	UPROPERTY(AssetRegistrySearchable)
	FName ItemTypeDisplayName = TEXT("Control Rig");

	UPROPERTY(AssetRegistrySearchable)
	FRigModuleSettings RigModuleSettings;

	/** Asset searchable information module references in this rig */
	UPROPERTY(AssetRegistrySearchable)
	TArray<FModuleReferenceData> ModuleReferenceData;

	// This relates to FAssetThumbnailPool::CustomThumbnailTagName and allows
	// the thumbnail pool to show the thumbnail of the icon rather than the
	// rig itself to avoid deploying the 3D renderer.
	UPROPERTY(AssetRegistrySearchable)
	FString CustomThumbnail;

	/** Whether or not this rig has an Inversion Event */
	UPROPERTY(AssetRegistrySearchable)
	bool bSupportsInversion;

	/** Whether or not this rig has Controls on It */
	UPROPERTY(AssetRegistrySearchable)
	bool bSupportsControls;

	UE_API bool IsControlRigModule() const;
};

#undef UE_API

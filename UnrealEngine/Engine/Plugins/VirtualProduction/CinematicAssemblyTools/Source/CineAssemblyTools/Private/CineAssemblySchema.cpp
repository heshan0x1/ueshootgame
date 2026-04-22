// Copyright Epic Games, Inc. All Rights Reserved.

#include "CineAssemblySchema.h"

#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Modules/ModuleManager.h"

#if WITH_EDITOR
#include "AssetToolsModule.h"
#endif

const FName UCineAssemblySchema::SchemaGuidPropertyName = GET_MEMBER_NAME_CHECKED(UCineAssemblySchema, SchemaGuid);

UCineAssemblySchema::UCineAssemblySchema()
{
}

FGuid UCineAssemblySchema::GetSchemaGuid() const
{
	return SchemaGuid;
}

void UCineAssemblySchema::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);
	for (FAssemblyMetadataDesc& Desc : AssemblyMetadata)
	{
		Ar << Desc.DefaultValue;
	}
}

void UCineAssemblySchema::PostInitProperties()
{
	Super::PostInitProperties();

	if (!HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject | RF_NeedLoad | RF_WasLoaded) && !SchemaGuid.IsValid())
	{
		SchemaGuid = FGuid::NewGuid();
	}
}

void UCineAssemblySchema::PostDuplicate(bool bDuplicateForPIE)
{
	Super::PostDuplicate(bDuplicateForPIE);

	if (!bDuplicateForPIE)
	{
		SchemaGuid = FGuid::NewGuid();

		// Change the name property to match the actual name of the duplicated asset
		SchemaName = this->GetName();
	}
}

void UCineAssemblySchema::PostLoad()
{
	Super::PostLoad();

	if (!SchemaGuid.IsValid())
	{
		SchemaGuid = FGuid::NewGuid();
	}
}

bool UCineAssemblySchema::SupportsRename() const
{
	return bSupportsRename;
}

#if WITH_EDITOR
void UCineAssemblySchema::RenameAsset(const FString& InNewName)
{
	// Early-out if the input name already matches the name of this schema
	if (SchemaName == InNewName)
	{
		return;
	}

	// If this schema does not yet have a valid package yet (i.e. it is still being configured), then there is no need to use Asset Tools to rename it
	if (GetPackage() == GetTransientPackage())
	{
		SchemaName = InNewName;
		return;
	}

	// The default behavior for schema assets is to not allow renaming from Content Browser.
	// However, this function relies on renaming being supported, so we temporarily enable to do the programmatic rename.
	bSupportsRename = true;

	IAssetTools& AssetTools = FModuleManager::GetModuleChecked<FAssetToolsModule>("AssetTools").Get();

	const FString PackagePath = FPackageName::GetLongPackagePath(GetOutermost()->GetName());

	TArray<FAssetRenameData> AssetsAndNames;
	const bool bSoftReferenceOnly = false;
	const bool bAlsoRenameLocalizedVariants = true;
	AssetsAndNames.Emplace(FAssetRenameData(this, PackagePath, InNewName, bSoftReferenceOnly, bAlsoRenameLocalizedVariants));

	EAssetRenameResult Result = AssetTools.RenameAssetsWithDialog(AssetsAndNames);
	if (Result != EAssetRenameResult::Failure)
	{
		SchemaName = InNewName;
	}

	bSupportsRename = false;
}
#endif

// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetDefinition_ProceduralVegetation.h"
#include "PVEditor.h"
#include "ProceduralVegetation.h"

FText UAssetDefinition_ProceduralVegetation::GetAssetDisplayName() const
{
	return NSLOCTEXT("AssetDefinition_ProceduralVegetation", "AssetDisplayName", "Procedural Vegetation");
}

FLinearColor UAssetDefinition_ProceduralVegetation::GetAssetColor() const
{
	return FColor::Green;
}

TSoftClassPtr<UObject> UAssetDefinition_ProceduralVegetation::GetAssetClass() const
{
	return UProceduralVegetation::StaticClass();
}

TConstArrayView<FAssetCategoryPath> UAssetDefinition_ProceduralVegetation::GetAssetCategories() const
{
	static const auto Categories = { EAssetCategoryPaths::Foliage };
	return Categories;
}

EAssetCommandResult UAssetDefinition_ProceduralVegetation::OpenAssets(const FAssetOpenArgs& OpenArgs) const
{
	for (UProceduralVegetation* ProceduralVegetation : OpenArgs.LoadObjects<UProceduralVegetation>())
	{
		const TSharedRef<FPVEditor> PVEditor = MakeShared<FPVEditor>();
		PVEditor->Initialize(EToolkitMode::Standalone, OpenArgs.ToolkitHost, ProceduralVegetation);
	}

	return EAssetCommandResult::Handled;
}

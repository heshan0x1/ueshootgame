// Copyright Epic Games, Inc. All Rights Reserved.

#include "ComputeFramework/ComputeGraphFromTextAssetActions.h"

#include "ComputeFramework/ComputeGraphFromText.h"

FAssetTypeActions_ComputeGraphFromText::FAssetTypeActions_ComputeGraphFromText(EAssetTypeCategories::Type InAssetCategoryBit)
	: AssetCategoryBit(InAssetCategoryBit)
{
}

FText FAssetTypeActions_ComputeGraphFromText::GetName() const
{
	return NSLOCTEXT("ComputeFramework", "ComputeGraphFromTextName", "Compute Graph (Text)");
}

FColor FAssetTypeActions_ComputeGraphFromText::GetTypeColor() const
{
	return FColor::Turquoise;
}

UClass* FAssetTypeActions_ComputeGraphFromText::GetSupportedClass() const
{
	return UComputeGraphFromText::StaticClass();
}

uint32 FAssetTypeActions_ComputeGraphFromText::GetCategories()
{
	return AssetCategoryBit;
}

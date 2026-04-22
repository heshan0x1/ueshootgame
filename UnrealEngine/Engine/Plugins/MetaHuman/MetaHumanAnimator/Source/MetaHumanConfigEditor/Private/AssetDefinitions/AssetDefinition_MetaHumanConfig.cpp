// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetDefinition_MetaHumanConfig.h"
#include "MetaHumanConfig.h"
#include "MetaHumanCoreEditorModule.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AssetDefinition_MetaHumanConfig)

FText UAssetDefinition_MetaHumanConfig::GetAssetDisplayName() const
{
	return NSLOCTEXT("MetaHuman", "ConfigAssetName", "MetaHuman Config");
}

FLinearColor UAssetDefinition_MetaHumanConfig::GetAssetColor() const
{
	return FColor::Orange;
}

TSoftClassPtr<UObject> UAssetDefinition_MetaHumanConfig::GetAssetClass() const
{
	return UMetaHumanConfig::StaticClass();
}

TConstArrayView<FAssetCategoryPath> UAssetDefinition_MetaHumanConfig::GetAssetCategories() const
{
	return FModuleManager::GetModuleChecked<IMetaHumanCoreEditorModule>(TEXT("MetaHumanCoreEditor")).GetMetaHumanAdvancedAssetCategoryPath();
}

// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetDefinition_MetaHumanCaptureSource.h"
#include "MetaHumanCaptureSource.h"
#include "MetaHumanCoreEditorModule.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AssetDefinition_MetaHumanCaptureSource)

FText UAssetDefinition_MetaHumanCaptureSource::GetAssetDisplayName() const
{
	return NSLOCTEXT("MetaHuman", "MetaHumanCaptureSourceAssetName", "Capture Source");
}

FLinearColor UAssetDefinition_MetaHumanCaptureSource::GetAssetColor() const
{
	return FColor::Yellow;
}

TSoftClassPtr<UObject> UAssetDefinition_MetaHumanCaptureSource::GetAssetClass() const
{
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	return UMetaHumanCaptureSource::StaticClass();
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

TConstArrayView<FAssetCategoryPath> UAssetDefinition_MetaHumanCaptureSource::GetAssetCategories() const
{
	return FModuleManager::GetModuleChecked<IMetaHumanCoreEditorModule>(TEXT("MetaHumanCoreEditor")).GetMetaHumanAssetCategoryPath();
}

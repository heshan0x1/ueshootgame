// Copyright Epic Games, Inc. All Rights Reserved.

#include "MaterialCacheVirtualTextureTagAssetTypeActions.h"
#include "ContentBrowserModule.h"
#include "MaterialCache/MaterialCacheVirtualTextureTag.h"

#define LOCTEXT_NAMESPACE "MaterialCacheVirtualTextureTagEditorModule"

UClass* FAssetTypeActions_MaterialCacheVirtualTextureTag::GetSupportedClass() const
{
	return UMaterialCacheVirtualTextureTag::StaticClass();
}

FText FAssetTypeActions_MaterialCacheVirtualTextureTag::GetName() const
{
	return LOCTEXT("AssetTypeActions_MaterialCacheVirtualTextureTag", "Material Cache Virtual Texture Tag"); 
}

FColor FAssetTypeActions_MaterialCacheVirtualTextureTag::GetTypeColor() const 
{
	return FColor(255, 100, 100); 
}

uint32 FAssetTypeActions_MaterialCacheVirtualTextureTag::GetCategories() 
{
	return EAssetTypeCategories::Textures; 
}

void FAssetTypeActions_MaterialCacheVirtualTextureTag::GetActions(TArray<UObject*> const& InObjects, FMenuBuilder& MenuBuilder)
{
	
}

#undef LOCTEXT_NAMESPACE

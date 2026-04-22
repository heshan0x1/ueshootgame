// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetDefinition_CineAssemblySchema.h"

#include "CineAssemblySchema.h"
#include "CineAssemblyToolsEditorModule.h"
#include "CineAssemblyToolsStyle.h"
#include "UI/CineAssembly/SCineAssemblySchemaWindow.h"

#define LOCTEXT_NAMESPACE "AssetTypeActions"

TSoftClassPtr<> UAssetDefinition_CineAssemblySchema::GetAssetClass() const
{
	return UCineAssemblySchema::StaticClass();
}

FText UAssetDefinition_CineAssemblySchema::GetAssetDisplayName() const
{
	return LOCTEXT("AssetTypeActions_CineAssemblySchema", "Cine Assembly Schema");
}

FLinearColor UAssetDefinition_CineAssemblySchema::GetAssetColor() const
{
	return FColor(176, 58, 104);
}

TConstArrayView<FAssetCategoryPath> UAssetDefinition_CineAssemblySchema::GetAssetCategories() const
{
	static const FAssetCategoryPath Categories[] = { EAssetCategoryPaths::Cinematics };
	return Categories;
}

const FSlateBrush* UAssetDefinition_CineAssemblySchema::GetThumbnailBrush(const FAssetData& InAssetData, const FName InClassName) const
{
	if (UCineAssemblySchema* CineAssemblySchema = Cast<UCineAssemblySchema>(InAssetData.GetAsset()))
	{
		FCineAssemblyToolsStyle& Style = FCineAssemblyToolsStyle::Get();

		// Get the thumbnail brush associated with this schema asset
		const FName ThumbnailBrushName = Style.GetThumbnailBrushNameForSchema(CineAssemblySchema->SchemaName);
		const FSlateBrush* ThumbnailBrush = Style.GetBrush(ThumbnailBrushName);

		// If no thumbnail brush exists yet for this schema, create a new one
		if (!ThumbnailBrush || ThumbnailBrush == Style.GetDefaultBrush())
		{
			Style.SetThumbnailBrushTextureForSchema(CineAssemblySchema->SchemaName, CineAssemblySchema->ThumbnailImage);
			ThumbnailBrush = Style.GetBrush(ThumbnailBrushName);
		}

		return ThumbnailBrush;
	}

	return FCineAssemblyToolsStyle::Get().GetBrush("ClassThumbnail.CineAssemblySchema");
}

FAssetSupportResponse UAssetDefinition_CineAssemblySchema::CanRename(const FAssetData& InAsset) const
{
	if (UCineAssemblySchema* CineAssemblySchema = Cast<UCineAssemblySchema>(InAsset.GetAsset()))
	{
		if (!CineAssemblySchema->SupportsRename())
		{
			return FAssetSupportResponse::NotSupported();
		}
	}

	return FAssetSupportResponse::Supported();
}

EAssetCommandResult UAssetDefinition_CineAssemblySchema::OpenAssets(const FAssetOpenArgs& OpenArgs) const
{
	for (const FAssetData& SchemaAssetData : OpenArgs.Assets)
	{
		if (UCineAssemblySchema* CineAssemblySchema = Cast<UCineAssemblySchema>(SchemaAssetData.GetAsset()))
		{
			FCineAssemblyToolsEditorModule& CineAssemblyToolsEditorModule = FModuleManager::GetModuleChecked<FCineAssemblyToolsEditorModule>("CineAssemblyToolsEditor");
			CineAssemblyToolsEditorModule.OpenSchemaForEdit(CineAssemblySchema);
		}
	}

	return EAssetCommandResult::Handled;
}

#undef LOCTEXT_NAMESPACE

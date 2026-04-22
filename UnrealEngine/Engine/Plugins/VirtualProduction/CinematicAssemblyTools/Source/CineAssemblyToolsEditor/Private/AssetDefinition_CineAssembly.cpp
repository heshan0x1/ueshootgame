// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetDefinition_CineAssembly.h"
#include "AssetToolsModule.h"
#include "CineAssembly.h"
#include "CineAssemblyToolsStyle.h"

#include "Editor.h"
#include "FileHelpers.h"

#define LOCTEXT_NAMESPACE "AssetTypeActions"

DEFINE_LOG_CATEGORY_STATIC(LogCineAssemblyDefinition, Log, All)

FText UAssetDefinition_CineAssembly::GetAssetDisplayName() const
{
	return LOCTEXT("AssetTypeActions_CineAssembly", "Cine Assembly");
}

FText UAssetDefinition_CineAssembly::GetAssetDisplayName(const FAssetData& AssetData) const
{
	const FAssetDataTagMapSharedView::FFindTagResult AssemblyType = AssetData.TagsAndValues.FindTag(UCineAssembly::AssetRegistryTag_AssemblyType);

	if (AssemblyType.IsSet())
	{
		return FText::FromString(AssemblyType.GetValue());
	}

	return GetAssetDisplayName();
}

FText UAssetDefinition_CineAssembly::GetAssetDescription(const FAssetData& AssetData) const
{
	if (const UCineAssembly* const CineAssembly = Cast<UCineAssembly>(AssetData.GetAsset()))
	{
		if (!CineAssembly->AssemblyNote.IsEmpty())
		{
			return FText::FromString(CineAssembly->AssemblyNote);
		}
	}

	return FText::GetEmpty();
}

TSoftClassPtr<> UAssetDefinition_CineAssembly::GetAssetClass() const
{
	return UCineAssembly::StaticClass();
}

FLinearColor UAssetDefinition_CineAssembly::GetAssetColor() const
{
	return FColor(229, 45, 113);
}

TConstArrayView<FAssetCategoryPath> UAssetDefinition_CineAssembly::GetAssetCategories() const
{
	static const FAssetCategoryPath Categories[] = { EAssetCategoryPaths::Cinematics };
	return Categories;
}

const FSlateBrush* UAssetDefinition_CineAssembly::GetThumbnailBrush(const FAssetData& InAssetData, const FName InClassName) const
{
	if (const UCineAssembly* const CineAssembly = Cast<UCineAssembly>(InAssetData.GetAsset()))
	{
		const UCineAssemblySchema* Schema = CineAssembly->GetSchema();
		if (Schema && Schema->ThumbnailImage)
		{
			// Use the thumbnail brush associated with the schema
			FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
			TSharedPtr<IAssetTypeActions> AssetTypeActions = AssetToolsModule.Get().GetAssetTypeActionsForClass(UCineAssemblySchema::StaticClass()).Pin();

			if (AssetTypeActions)
			{
				const FAssetData AssetData = FAssetData(Schema);
				const FName AssetClassName = AssetData.AssetClassPath.GetAssetName();

				return AssetTypeActions->GetThumbnailBrush(AssetData, AssetClassName);
			}
		}
	}

	return FCineAssemblyToolsStyle::Get().GetBrush("ClassThumbnail.CineAssembly");
}

EAssetCommandResult UAssetDefinition_CineAssembly::OpenAssets(const FAssetOpenArgs& OpenArgs) const
{
	TArray<FAssetData> DataOnlyAssetData;
	TArray<FAssetData> FullAssetData;
	
	const FName IsDataOnlyAssetRegistrySearchablePropertyName = GET_MEMBER_NAME_CHECKED(UCineAssembly, bIsDataOnly);

	for (const FAssetData& AssetData : OpenArgs.Assets)
	{
		bool bAssemblyIsDataOnly;
		AssetData.GetTagValue(IsDataOnlyAssetRegistrySearchablePropertyName, bAssemblyIsDataOnly);

		if (bAssemblyIsDataOnly)
		{
			DataOnlyAssetData.Add(AssetData);
		}
		else
		{
			FullAssetData.Add(AssetData);
		}
	}

	FAssetOpenArgs DataOnlyOpenArgs(OpenArgs);
	DataOnlyOpenArgs.Assets = TConstArrayView<FAssetData>(DataOnlyAssetData);
	FAssetOpenArgs FullAssetOpenArgs(OpenArgs);
	FullAssetOpenArgs.Assets = TConstArrayView<FAssetData>(FullAssetData);

	if (!DataOnlyOpenArgs.Assets.IsEmpty())
	{
		UAssetDefinitionDefault::OpenAssets(DataOnlyOpenArgs);
	}

	return Super::OpenAssets(FullAssetOpenArgs);
}

TArray<FAssetData> UAssetDefinition_CineAssembly::PrepareToActivateAssets(const FAssetActivateArgs& ActivateArgs) const
{
	TArray<FAssetData> AssetsToOpen;

	// We only support opening one asset at a time
	if (ActivateArgs.Assets.Num() == 1)
	{
		const FAssetData& CineAssemblyData = ActivateArgs.Assets[0];
		AssetsToOpen.Add(CineAssemblyData);

		const FName IsDataOnlyAssetRegistrySearchablePropertyName = GET_MEMBER_NAME_CHECKED(UCineAssembly, bIsDataOnly);

		bool bAssemblyIsDataOnly;
		CineAssemblyData.GetTagValue(IsDataOnlyAssetRegistrySearchablePropertyName, bAssemblyIsDataOnly);

		UCineAssembly* CineAssembly = Cast<UCineAssembly>(CineAssemblyData.GetAsset());
		if (!bAssemblyIsDataOnly && CineAssembly != nullptr)
		{
			if (CineAssembly->Level.IsValid())
			{
				if (UWorld* WorldToOpen = Cast<UWorld>(CineAssembly->Level.TryLoad()))
				{
					UWorld* CurrentWorld = GEditor->GetEditorWorldContext().World();
					if (CurrentWorld != WorldToOpen)
					{
						// Prompt the user to save their unsaved changes to the current level before loading the level associated with this asset
						constexpr bool bPromptUserToSave = true;
						constexpr bool bSaveMapPackages = true;
						constexpr bool bSaveContentPackages = false;
						if (FEditorFileUtils::SaveDirtyPackages(bPromptUserToSave, bSaveMapPackages, bSaveContentPackages))
						{
							if (!WorldToOpen->GetPackage()->HasAnyPackageFlags(PKG_NewlyCreated))
							{
								const FString FileToOpen = FPackageName::LongPackageNameToFilename(WorldToOpen->GetOutermost()->GetName(), FPackageName::GetMapPackageExtension());
								const bool bLoadAsTemplate = false;
								const bool bShowProgress = true;
								FEditorFileUtils::LoadMap(FileToOpen, bLoadAsTemplate, bShowProgress);
							}
						}
						else
						{
							// If the user canceled out of the prompt to save the current level, then do not try to open the asset
							AssetsToOpen.Empty();
						}
					}
				}
				else
				{
					UE_LOG(LogCineAssemblyDefinition, Error, TEXT("Failed to load %s while opening %s"), *CineAssembly->Level.ToString(), *CineAssembly->GetFName().ToString());
				}
			}
		}
	}

	return AssetsToOpen;
}

#undef LOCTEXT_NAMESPACE

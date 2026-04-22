// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanSequenceExportCustomization.h"
#include "MetaHumanPerformanceExportUtils.h"

#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "PropertyHandle.h"
#include "PropertyCustomizationHelpers.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Blueprint.h"

#define LOCTEXT_NAMESPACE "MetaHumanSequenceExportCustomization"

TSharedRef<IDetailCustomization> FMetaHumanSequenceExportCustomization::MakeInstance()
{
	return MakeShared<FMetaHumanSequenceExportCustomization>();
}

void FMetaHumanSequenceExportCustomization::CustomizeDetails(IDetailLayoutBuilder& InDetailBuilder)
{
	TSharedRef<IPropertyHandle> TargetProp = InDetailBuilder.GetProperty(
		GET_MEMBER_NAME_CHECKED(UMetaHumanPerformanceExportLevelSequenceSettings, TargetMetaHumanClass),UMetaHumanPerformanceExportLevelSequenceSettings::StaticClass());

	// Customize the *existing* row (keeps DisplayAfter/EditCondition/etc.)
	IDetailPropertyRow* PropertyRow = InDetailBuilder.EditDefaultProperty(TargetProp);

	PropertyRow->CustomWidget()
	.NameContent()
	[
		TargetProp->CreatePropertyNameWidget()   // uses UPROPERTY DisplayName/tooltip
	]
	.ValueContent()
	.MinDesiredWidth(400.f)
	[
		// Filtered picker; shows the same icon/thumbnail as stock
		SNew(SObjectPropertyEntryBox)
		.PropertyHandle(TargetProp)
		.AllowedClass(UBlueprint::StaticClass())
		.AllowClear(true)
		.DisplayThumbnail(true)
		.ThumbnailPool(InDetailBuilder.GetThumbnailPool())
		.OnShouldFilterAsset(FOnShouldFilterAsset::CreateSP(this, &FMetaHumanSequenceExportCustomization::ShouldFilterBlueprint))
	];
}

// This effectively acts in a similar way to changing TargetMetaHumanClass to be TSubclassOf<AActor>
// but without breaking the existing interface and opening opportunity for more custom filtering
bool FMetaHumanSequenceExportCustomization::ShouldFilterBlueprint(const FAssetData& InAssetData) const
{
	FString ParentPath;
	if (!InAssetData.GetTagValue(FBlueprintTags::ParentClassPath, ParentPath) || ParentPath.IsEmpty())
	{
		return true; // reject as no parent info
	}

	// This both loads if needed and enforces AActor inheritance
	UClass* ParentClass = LoadClass<AActor>(nullptr, *ParentPath);
	if (!ParentClass)
	{
		return true; // not an Actor or couldn’t load
	}

	return false;
}

#undef LOCTEXT_NAMESPACE

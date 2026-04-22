// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartitionHLODLayerDetailsCustomization.h"
#include "DetailLayoutBuilder.h"

#include "WorldPartition/HLOD/HLODLayer.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionRuntimeSpatialHash.h"

#define LOCTEXT_NAMESPACE "FWorldPartitionHLODLayerDetailsCustomization"

TSharedRef<IDetailCustomization> FWorldPartitionHLODLayerDetailsCustomization::MakeInstance()
{
	return MakeShareable(new FWorldPartitionHLODLayerDetailsCustomization);
}

void FWorldPartitionHLODLayerDetailsCustomization::CustomizeDetails(IDetailLayoutBuilder& InDetailLayoutBuilder)
{
	DetailLayoutBuilder = &InDetailLayoutBuilder;

	UWorldPartition::WorldPartitionChangedEvent.RemoveAll(this);
	UWorldPartition::WorldPartitionChangedEvent.AddSP(this, &FWorldPartitionHLODLayerDetailsCustomization::OnWorldPartitionChanged);

	UHLODLayer* HLODLayer = !DetailLayoutBuilder->GetSelectedObjects().IsEmpty() ? Cast<UHLODLayer>(DetailLayoutBuilder->GetSelectedObjects()[0].Get()) : nullptr;
	if (!HLODLayer)
	{
		return;
	}
		
	TSharedPtr<IPropertyHandle> IsSpatiallyLoadedPropertyHandle = DetailLayoutBuilder->GetProperty(GET_MEMBER_NAME_CHECKED(UHLODLayer, bIsSpatiallyLoaded), UHLODLayer::StaticClass());
	TSharedPtr<IPropertyHandle> CellSizePropertyHandle = DetailLayoutBuilder->GetProperty(GET_MEMBER_NAME_CHECKED(UHLODLayer, CellSize), UHLODLayer::StaticClass());
	TSharedPtr<IPropertyHandle> LoadingRangePropertyHandle = DetailLayoutBuilder->GetProperty(GET_MEMBER_NAME_CHECKED(UHLODLayer, LoadingRange), UHLODLayer::StaticClass());
	TSharedPtr<IPropertyHandle> ParentLayerPropertyHandle = DetailLayoutBuilder->GetProperty(GET_MEMBER_NAME_CHECKED(UHLODLayer, ParentLayer), UHLODLayer::StaticClass());

	bool bShowLegacyHLODLayerProperties = false;
	if (UWorld* EditorWorld = GEditor->GetEditorWorldContext().World())
	{
		if (UWorldPartition* WorldPartition = EditorWorld->GetWorldPartition())
		{
			bShowLegacyHLODLayerProperties = Cast<UWorldPartitionRuntimeSpatialHash>(WorldPartition->RuntimeHash) != nullptr;
		}
	}
	
	if (!bShowLegacyHLODLayerProperties)
	{
		DetailLayoutBuilder->HideProperty(IsSpatiallyLoadedPropertyHandle);
		DetailLayoutBuilder->HideProperty(CellSizePropertyHandle);
		DetailLayoutBuilder->HideProperty(LoadingRangePropertyHandle);
	}
	else
	{
		IsSpatiallyLoadedPropertyHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateLambda([this]()
		{
			DetailLayoutBuilder->ForceRefreshDetails();
		}));

		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		const bool bIsSpatiallyLoaded = HLODLayer->IsSpatiallyLoaded();
		PRAGMA_ENABLE_DEPRECATION_WARNINGS

		if (!bIsSpatiallyLoaded)
		{
			DetailLayoutBuilder->HideProperty(CellSizePropertyHandle);
			DetailLayoutBuilder->HideProperty(LoadingRangePropertyHandle);
			DetailLayoutBuilder->HideProperty(ParentLayerPropertyHandle);
		}
	}
}


FWorldPartitionHLODLayerDetailsCustomization::~FWorldPartitionHLODLayerDetailsCustomization()
{
	UWorldPartition::WorldPartitionChangedEvent.RemoveAll(this);
}

void FWorldPartitionHLODLayerDetailsCustomization::OnWorldPartitionChanged(UWorld* InWorld)
{
	DetailLayoutBuilder->ForceRefreshDetails();
}

#undef LOCTEXT_NAMESPACE

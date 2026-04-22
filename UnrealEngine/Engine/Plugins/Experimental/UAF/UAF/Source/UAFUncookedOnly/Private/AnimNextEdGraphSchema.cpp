// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimNextEdGraphSchema.h"

#include "AnimNextRigVMAsset.h"
#include "Editor.h"
#include "Editor/EditorEngine.h"
#include "Entries/AnimNextRigVMAssetEntry.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AnimNextEdGraphSchema)

#define LOCTEXT_NAMESPACE "AnimNextEdGraphSchema"

void UAnimNextEdGraphSchema::GetGraphDisplayInformation(const UEdGraph& Graph, /*out*/ FGraphDisplayInfo& DisplayInfo) const
{
	Super::GetGraphDisplayInformation(Graph, DisplayInfo);
	
	if(UAnimNextRigVMAssetEntry* AssetEntry = Cast<UAnimNextRigVMAssetEntry>(Graph.GetOuter()))
	{
		DisplayInfo.DisplayName = FText::Format(LOCTEXT("GraphTabTitleFormat", "{0}: {1}"), FText::FromName(AssetEntry->GetEntryName()), FText::FromName(AssetEntry->GetTypedOuter<UAnimNextRigVMAsset>()->GetFName()));
		DisplayInfo.Tooltip = FText::Format(LOCTEXT("GraphTabTooltipFormat", "{0} in:\n{1}"), FText::FromName(AssetEntry->GetEntryName()), FText::FromString(AssetEntry->GetTypedOuter<UAnimNextRigVMAsset>()->GetPathName()));
	}
}

TSharedPtr<IAssetReferenceFilter> UAnimNextEdGraphSchema::MakeAssetReferenceFilter(const UEdGraph* Graph)
{
	if (Graph)
	{
		if (UAnimNextRigVMAsset* Asset = Cast<UAnimNextRigVMAsset>(Graph->GetTypedOuter<UAnimNextRigVMAsset>()))
		{
			if (GEditor)
			{
				FAssetReferenceFilterContext AssetReferenceFilterContext;
				AssetReferenceFilterContext.AddReferencingAsset(Asset);
				return GEditor->MakeAssetReferenceFilter(AssetReferenceFilterContext);
			}
		}
	}

	return {};
}

#undef LOCTEXT_NAMESPACE

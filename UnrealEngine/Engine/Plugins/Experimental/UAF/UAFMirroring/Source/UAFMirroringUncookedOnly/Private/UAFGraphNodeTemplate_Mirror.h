// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AnimGraphUncookedOnlyUtils.h"
#include "AnimNextController.h"
#include "Styling/AppStyle.h"
#include "Styling/SlateIconFinder.h"
#include "Templates/UAFGraphNodeTemplate.h"
#include "MirroringTraitData.h"
#include "Animation/MirrorDataTable.h"

#include "UAFGraphNodeTemplate_Mirror.generated.h"

#define LOCTEXT_NAMESPACE "UAFGraphNodeTemplate_Mirror"

UCLASS()
class UUAFGraphNodeTemplate_Mirror : public UUAFGraphNodeTemplate
{
	GENERATED_BODY()

	UUAFGraphNodeTemplate_Mirror()
	{
		Title = LOCTEXT("MirrorTitle", "Mirror");
		TooltipText = LOCTEXT("MirrorTooltip", "Mirror an input using a mirror data table");
		Category = LOCTEXT("MirrorCategory", "UAF");
		MenuDescription = LOCTEXT("MirrorMenuDesc", "Mirror");
		Color = FLinearColor(FColor(62, 140, 35)); // From UAssetDefinition_DataTable
		Icon = *FSlateIconFinder::FindIconForClass(UMirrorDataTable::StaticClass()).GetIcon();
		Traits =
		{
			TInstancedStruct<UE::UAF::FMirroringTraitData>::Make(),
		};
		DragDropAssetTypes.Add(UMirrorDataTable::StaticClass());
		SetCategoryForPinsInLayout(
			{
				GET_PIN_PATH_STRING_CHECKED(FMirroringTraitSharedData, Input),
				GET_PIN_PATH_STRING_CHECKED(FMirroringTraitSharedData, Setup),
				GET_PIN_PATH_STRING_CHECKED(FMirroringTraitSharedData, ApplyTo),
			},
			FRigVMPinCategory::GetDefaultCategoryName(),
			NodeLayout,
			true);
	}

	virtual void HandleAssetDropped_Implementation(UAnimNextController* Controller, URigVMUnitNode* Node, UObject* Asset) const
	{
		Super::HandleAssetDropped_Implementation(Controller, Node, Asset);

		// @todo: Wish we could use a subtitle instead but looks like the subtitle can't be set via the controller as its not instanced per not but rather queried per template.
		
		if (Asset)
		{
			Controller->OpenUndoBracket(LOCTEXT("ConfigureNodeOnDrop", "Configure Node On Drop").ToString());

			Controller->SetNodeTitle(Node, FText::Format(LOCTEXT("NodeTitleFormat", "Mirror using {0}"), FText::FromString(Asset->GetName())).ToString(), true, true, true);
			
			Controller->CloseUndoBracket();
		}
	}

	virtual void HandlePinDefaultValueChanged_Implementation(UAnimNextController* Controller, URigVMPin* Pin) const
	{
		Super::HandlePinDefaultValueChanged_Implementation(Controller, Pin);

		if (Pin->GetFName() == GET_MEMBER_NAME_CHECKED(UE::UAF::FMirroringTraitSetupParams, MirrorDataTable))
		{
			Controller->OpenUndoBracket(LOCTEXT("SetNodeTitle", "Set Node Title").ToString());

			FSoftObjectPath Path(Pin->GetDefaultValue());
			Controller->SetNodeTitle(Pin->GetNode(), FText::Format(LOCTEXT("NodeTitleFormat", "Mirror using {0}"), FText::FromString(Path.GetAssetName())).ToString(), true, true, true);

			Controller->CloseUndoBracket();
		}
	}
};

#undef LOCTEXT_NAMESPACE
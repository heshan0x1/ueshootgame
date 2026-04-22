// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuCOE/Nodes/CONodeMaterialBreak.h"

#include "DetailsViewArgs.h"
#include "MuCOE/EdGraphSchema_CustomizableObject.h"
#include "PropertyEditorModule.h"


#include UE_INLINE_GENERATED_CPP_BY_NAME(CONodeMaterialBreak)

class UCustomizableObjectNodeRemapPins;
struct FPropertyChangedEvent;
struct FSlateBrush;

#define LOCTEXT_NAMESPACE "CustomizableObjectEditor"


bool UCustomizableObjectNodeBreakMaterialRemapPins::Equal(const UCustomizableObjectNode& Node, const UEdGraphPin& OldPin, const UEdGraphPin& NewPin) const
{
	UCONodeMaterialBreakPinData* OldPinData = Cast<UCONodeMaterialBreakPinData>(Node.GetPinData(OldPin));
	UCONodeMaterialBreakPinData* NewPinData = Cast<UCONodeMaterialBreakPinData>(Node.GetPinData(NewPin));

	if (OldPinData && NewPinData)
	{
		return OldPin.Direction == NewPin.Direction && OldPinData->VariableData.Type == NewPinData->VariableData.Type && OldPinData->VariableData.Id == NewPinData->VariableData.Id;
	}
	else
	{
		return Super::Equal(Node, OldPin, NewPin);
	}
}


void UCONodeMaterialBreak::AllocateDefaultPins(UCustomizableObjectNodeRemapPins* RemapPins)
{
	const FName Type = UEdGraphSchema_CustomizableObject::PC_Material;
	const FName PinName = UEdGraphSchema_CustomizableObject::GetPinCategoryName(Type);
	const FText PinFriendlyName = UEdGraphSchema_CustomizableObject::GetPinCategoryFriendlyName(Type);
	
	UEdGraphPin* MaterialPin = CustomCreatePin(EGPD_Input, Type, PinName);
	MaterialPin->PinFriendlyName = PinFriendlyName;

	MaterialPinRef = FEdGraphPinReference(MaterialPin);

	// Get old pins
	TArray<UEdGraphPin*> OutputPins;
	GetOutputPins(OutputPins);

	// Regenerate the new pins from the old pin's pindata which may have changed
	for (const UEdGraphPin* Pin : OutputPins)
	{
		if (UCONodeMaterialBreakPinData* PinData = Cast<UCONodeMaterialBreakPinData>(GetPinData(*Pin)))
		{
			// Use the same pin data as the old pin since the new one is a copy
			UEdGraphPin* NewPin = CustomCreatePin(EGPD_Output, PinData->VariableData.Type, PinData->VariableData.Name, PinData);
		}
	}
}


FText UCONodeMaterialBreak::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return LOCTEXT("Material_Break", "Break Material");
}


FLinearColor UCONodeMaterialBreak::GetNodeTitleColor() const
{
	return UEdGraphSchema_CustomizableObject::GetPinTypeColor(UEdGraphSchema_CustomizableObject::PC_Material);
}


FText UCONodeMaterialBreak::GetTooltipText() const
{
	return LOCTEXT("Material_Break_Tooltip", "Allows to get the parameter value from the input material node.");
}


TSharedPtr<IDetailsView> UCONodeMaterialBreak::CustomizePinDetails(const UEdGraphPin& Pin) const
{
	if (UCONodeMaterialBreakPinData* PinData = Cast<UCONodeMaterialBreakPinData>(GetPinData(Pin)))
	{
		FPropertyEditorModule& EditModule = FModuleManager::Get().GetModuleChecked<FPropertyEditorModule>("PropertyEditor");

		FDetailsViewArgs DetailsViewArgs;
		DetailsViewArgs.bAllowSearch = false;
		DetailsViewArgs.bHideSelectionTip = true;

		const TSharedRef<IDetailsView> SettingsView = EditModule.CreateDetailView(DetailsViewArgs);
		SettingsView->SetObject(PinData);

		return SettingsView;
	}
	else
	{
		return nullptr;
	}
}


bool UCONodeMaterialBreak::HasPinViewer() const
{
	return true;
}


bool UCONodeMaterialBreak::CanCreatePinsFromPinViewer() const
{
	return true;
}


UCustomizableObjectNodeRemapPins* UCONodeMaterialBreak::CreateRemapPinsDefault() const
{
	return NewObject<UCustomizableObjectNodeBreakMaterialRemapPins>();
}


TArray<FName> UCONodeMaterialBreak::GetAllowedPinViewerCreationTypes() const
{
	return { UEdGraphSchema_CustomizableObject::PC_Texture, UEdGraphSchema_CustomizableObject::PC_Color, UEdGraphSchema_CustomizableObject::PC_Float };
}


void UCONodeMaterialBreak::CreatePinFromPinViewer()
{
	FName NewPinCategorery = UEdGraphSchema_CustomizableObject::PC_Float;
	
	UCONodeMaterialBreakPinData* PinData = NewObject<UCONodeMaterialBreakPinData>(this);
	PinData->VariableData.Name = "NewPin";
	PinData->VariableData.Type = NewPinCategorery;
	PinData->VariableData.Id = FGuid::NewGuid();

	CustomCreatePin(EEdGraphPinDirection::EGPD_Output, NewPinCategorery, "NewPin", PinData);

	GetGraph()->NotifyGraphChanged();
}


FName UCONodeMaterialBreak::GetPinParameterName(const UEdGraphPin& Pin) const
{
	if (const UCONodeMaterialBreakPinData* PinData = Cast<UCONodeMaterialBreakPinData>(GetPinData(Pin)))
	{
		return PinData->VariableData.Name;
	}

	return NAME_None;
}


#undef LOCTEXT_NAMESPACE

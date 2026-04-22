// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuCOE/Nodes/CustomizableObjectNodeColorToSRGB.h"

#include "MuCOE/EdGraphSchema_CustomizableObject.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(CustomizableObjectNodeColorToSRGB)

class UCustomizableObjectNodeRemapPins;

#define LOCTEXT_NAMESPACE "CustomizableObjectEditor"


UCustomizableObjectNodeColorToSRGB::UCustomizableObjectNodeColorToSRGB()
	: Super()
{
}


void UCustomizableObjectNodeColorToSRGB::AllocateDefaultPins(UCustomizableObjectNodeRemapPins* RemapPins)
{
	const UEdGraphSchema_CustomizableObject* Schema = GetDefault<UEdGraphSchema_CustomizableObject>();
	
	UEdGraphPin* InPin = CustomCreatePin(EGPD_Input, Schema->PC_Color, FName("Linear"));
	InPin->bDefaultValueIsIgnored = true;
	InputPin = InPin;

	UEdGraphPin* OutPin = CustomCreatePin(EGPD_Output, Schema->PC_Color, FName("sRGB"));
	OutPin->bDefaultValueIsIgnored = true;
	OutputPin = OutPin;
}


FText UCustomizableObjectNodeColorToSRGB::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return LOCTEXT("LinearColor_To_sRGB", "Linear To sRGB");
}


FLinearColor UCustomizableObjectNodeColorToSRGB::GetNodeTitleColor() const
{
	return UEdGraphSchema_CustomizableObject::GetPinTypeColor(UEdGraphSchema_CustomizableObject::PC_Color);
}


FText UCustomizableObjectNodeColorToSRGB::GetTooltipText() const
{
	return LOCTEXT("Color_TO_SRGB_Tooltip", "Converts a linear color to sRGB.");
}


UEdGraphPin* UCustomizableObjectNodeColorToSRGB::GetInputPin() const
{
	return InputPin.Get();
}


UEdGraphPin* UCustomizableObjectNodeColorToSRGB::GetOutputPin() const
{
	return OutputPin.Get();
}


#undef LOCTEXT_NAMESPACE


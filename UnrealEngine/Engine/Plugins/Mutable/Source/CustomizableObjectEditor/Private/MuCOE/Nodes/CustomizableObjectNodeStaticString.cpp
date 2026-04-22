// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuCOE/Nodes/CustomizableObjectNodeStaticString.h"

#include "MuCOE/EdGraphSchema_CustomizableObject.h"
#include "MuCOE/Nodes/SCustomizableObjectNode.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(CustomizableObjectNodeStaticString)

#define LOCTEXT_NAMESPACE "CustomizableObjectNodeStringConstant"


FText UCustomizableObjectNodeStaticString::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return LOCTEXT("StaticStringNodeTitle", "Static String");
}


FText UCustomizableObjectNodeStaticString::GetTooltipText() const
{
	return LOCTEXT("StaticStringNodeTooltip", "Static String Node");
}


FLinearColor UCustomizableObjectNodeStaticString::GetNodeTitleColor() const
{
	return UEdGraphSchema_CustomizableObject::GetPinTypeColor(UEdGraphSchema_CustomizableObject::PC_String);
}


void UCustomizableObjectNodeStaticString::AllocateDefaultPins(UCustomizableObjectNodeRemapPins* RemapPins)
{
	const UEdGraphSchema_CustomizableObject* Schema = GetDefault<UEdGraphSchema_CustomizableObject>();

	CustomCreatePin(EGPD_Output, Schema->PC_String, FName("Value"));

	UEdGraphPin* StringPin = CustomCreatePin(EGPD_Input, Schema->PC_String, FName("String"));
	StringPin->bNotConnectable = true;
}


bool UCustomizableObjectNodeStaticString::CanRenamePin(const UEdGraphPin& Pin) const
{
	return Pin.PinType.PinCategory == UEdGraphSchema_CustomizableObject::PC_String;
}


FText UCustomizableObjectNodeStaticString::GetPinEditableName(const UEdGraphPin& Pin) const
{
	return FText::FromString(Value);
}


void UCustomizableObjectNodeStaticString::SetPinEditableName(const UEdGraphPin& Pin, const FText& InValue)
{
	Value = InValue.ToString();
}


#undef LOCTEXT_NAMESPACE

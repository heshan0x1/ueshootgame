// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuCOE/Nodes/CONodeSchema.h"

#include "MuCOE/CustomizableObjectGraph.h"
#include "MuCOE/RemapPins/CustomizableObjectNodeRemapPinsByPosition.h"


FText UCONodeSchema::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return Title;
}


FLinearColor UCONodeSchema::GetNodeTitleColor() const
{
	return TitleColor;
}


void UCONodeSchema::AllocateDefaultPins(UCustomizableObjectNodeRemapPins* RemapPins)
{
	Super::AllocateDefaultPins(RemapPins);
	
	for (const FPinSchema& PinSchema : PinSchemas)
	{
		const EEdGraphPinDirection Direction = PinSchema.bIsInput ? EGPD_Input : EGPD_Output;

		UEdGraphPin* Pin = CreatePin(Direction, UEdGraphSchema_CustomizableObject::PC_Wildcard, FName(PinSchema.FriendlyName.ToString()));
		Pin->PinType.ContainerType = PinSchema.bIsArray ? EPinContainerType::Array : EPinContainerType::None;
		Pin->PinFriendlyName = PinSchema.FriendlyName;

		UCONodeSchemaPinData* PinData = NewObject<UCONodeSchemaPinData>(this);
		PinData->Color = PinSchema.Color;
		PinData->bIsPassthrough = PinSchema.bIsPassthrough;
		PinData->bIsEditable = PinSchema.bIsEditable;
		PinData->Name = Pin->PinFriendlyName;
		
		AddPinData(*Pin, *PinData);
	}
}


FLinearColor UCONodeSchema::GetPinColor(const UEdGraphPin& Pin) const
{
	return GetPinData<UCONodeSchemaPinData>(Pin).Color;
}


bool UCONodeSchema::IsPassthrough(const UEdGraphPin& Pin) const
{
	return GetPinData<UCONodeSchemaPinData>(Pin).bIsPassthrough;
}


bool UCONodeSchema::CanRenamePin(const UEdGraphPin& Pin) const
{
	return GetPinData<UCONodeSchemaPinData>(Pin).bIsEditable;
}


FText UCONodeSchema::GetPinEditableName(const UEdGraphPin& Pin) const
{
	return GetPinData<UCONodeSchemaPinData>(Pin).Name;
}


void UCONodeSchema::SetPinEditableName(const UEdGraphPin& Pin, const FText& Value)
{
	GetPinData<UCONodeSchemaPinData>(Pin).Name = Value;
}


UCustomizableObjectNodeRemapPins* UCONodeSchema::CreateRemapPinsDefault() const
{
	return NewObject<UCustomizableObjectNodeRemapPinsByPosition>();
}


void UCONodeSchema::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.Property)
	{
		if (PropertyChangedEvent.Property->GetFName() != GET_MEMBER_NAME_CHECKED(FPinSchema, Color) &&
			PropertyChangedEvent.Property->GetFName() != GET_MEMBER_NAME_CHECKED(UCONodeSchema, TitleColor))
		{
			ReconstructNode();
		}
	}
}

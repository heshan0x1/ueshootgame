// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuCOE/Nodes/CustomizableObjectNodeTextureFromChannels.h"

#include "MuCO/CustomizableObjectCustomVersion.h"
#include "MuCOE/EdGraphSchema_CustomizableObject.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(CustomizableObjectNodeTextureFromChannels)

class UCustomizableObjectNodeRemapPins;

#define LOCTEXT_NAMESPACE "CustomizableObjectEditor"


void UCustomizableObjectNodeTextureFromChannels::AllocateDefaultPins(UCustomizableObjectNodeRemapPins* RemapPins)
{
	const UEdGraphSchema_CustomizableObject* Schema = GetDefault<UEdGraphSchema_CustomizableObject>();

	FString PinName = TEXT("Texture");
	UEdGraphPin* OutputPin = CustomCreatePin(EGPD_Output, Schema->PC_Texture, FName(*PinName));
	OutputPin->bDefaultValueIsIgnored = true;

	PinName = TEXT("R");
	UEdGraphPin* RPin = CustomCreatePin(EGPD_Input, Schema->PC_Texture, FName(*PinName));
	RPin->bDefaultValueIsIgnored = true;

	PinName = TEXT("G");
	UEdGraphPin* GPin = CustomCreatePin(EGPD_Input, Schema->PC_Texture, FName(*PinName));
	GPin->bDefaultValueIsIgnored = true;

	PinName = TEXT("B");
	UEdGraphPin* BPin = CustomCreatePin(EGPD_Input, Schema->PC_Texture, FName(*PinName));
	BPin->bDefaultValueIsIgnored = true;

	PinName = TEXT("A");
	UEdGraphPin* APin = CustomCreatePin(EGPD_Input, Schema->PC_Texture, FName(*PinName));
	APin->bDefaultValueIsIgnored = true;
}


void UCustomizableObjectNodeTextureFromChannels::BackwardsCompatibleFixup(int32 CustomizableObjectCustomVersion)
{
	Super::BackwardsCompatibleFixup(CustomizableObjectCustomVersion);
	
	if (CustomizableObjectCustomVersion == FCustomizableObjectCustomVersion::FixPinsNamesImageToTexture2)
	{
		if (UEdGraphPin* TexturePin = FindPin(TEXT("Image")))
		{
			TexturePin->PinName = TEXT("Texture");
			UCustomizableObjectNode::ReconstructNode();
		}
	}
}


FText UCustomizableObjectNodeTextureFromChannels::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return LOCTEXT("Texture_From_Channels", "Make Texture");
}


FLinearColor UCustomizableObjectNodeTextureFromChannels::GetNodeTitleColor() const
{
	return UEdGraphSchema_CustomizableObject::GetPinTypeColor(UEdGraphSchema_CustomizableObject::PC_Texture);
}


FText UCustomizableObjectNodeTextureFromChannels::GetTooltipText() const
{
	return LOCTEXT("Texture_From_Channels_Tooltip", "Make a colored texture with transparency from four grayscale textures that represent the values of each RGBA channel.");
}

#undef LOCTEXT_NAMESPACE


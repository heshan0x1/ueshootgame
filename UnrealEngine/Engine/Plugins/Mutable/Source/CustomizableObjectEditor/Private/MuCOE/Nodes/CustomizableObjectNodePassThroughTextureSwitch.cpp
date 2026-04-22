// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuCOE/Nodes/CustomizableObjectNodePassThroughTextureSwitch.h"

#include "MuCOE/EdGraphSchema_CustomizableObject.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(CustomizableObjectNodePassThroughTextureSwitch)

#define LOCTEXT_NAMESPACE "CustomizableObjectEditor"


FName UCustomizableObjectNodePassThroughTextureSwitch::GetCategory() const
{
	return UEdGraphSchema_CustomizableObject::PC_PassthroughTexture;
}


#undef LOCTEXT_NAMESPACE


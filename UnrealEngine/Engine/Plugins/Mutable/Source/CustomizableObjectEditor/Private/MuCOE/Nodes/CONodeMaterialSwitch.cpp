// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuCOE/Nodes/CONodeMaterialSwitch.h"
#include "MuCOE/EdGraphSchema_CustomizableObject.h"
#include UE_INLINE_GENERATED_CPP_BY_NAME(CONodeMaterialSwitch)

#define LOCTEXT_NAMESPACE "CustomizableObjectEditor"


FName UCONodeMaterialSwitch::GetCategory() const
{
	return UEdGraphSchema_CustomizableObject::PC_Material;
}


#undef LOCTEXT_NAMESPACE


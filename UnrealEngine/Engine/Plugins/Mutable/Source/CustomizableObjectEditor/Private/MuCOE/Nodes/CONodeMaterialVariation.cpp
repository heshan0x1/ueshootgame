// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuCOE/Nodes/CONodeMaterialVariation.h"
#include "MuCOE/EdGraphSchema_CustomizableObject.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(CONodeMaterialVariation)

FName UCONodeMaterialVariation::GetCategory() const
{
	return UEdGraphSchema_CustomizableObject::PC_Material;
}

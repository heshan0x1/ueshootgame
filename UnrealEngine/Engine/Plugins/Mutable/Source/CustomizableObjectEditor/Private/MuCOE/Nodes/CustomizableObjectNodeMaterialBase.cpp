// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuCOE/Nodes/CustomizableObjectNodeMaterialBase.h"

#include "MuCOE/EdGraphSchema_CustomizableObject.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(CustomizableObjectNodeMaterialBase)

int32 UCustomizableObjectNodeMaterialBase::GetMeshSectionMaxLOD() const
{
	return MaxLOD;
}

FLinearColor UCustomizableObjectNodeMaterialBase::GetNodeTitleColor() const
{
	return UEdGraphSchema_CustomizableObject::GetPinTypeColor(UEdGraphSchema_CustomizableObject::PC_MeshSection);
}

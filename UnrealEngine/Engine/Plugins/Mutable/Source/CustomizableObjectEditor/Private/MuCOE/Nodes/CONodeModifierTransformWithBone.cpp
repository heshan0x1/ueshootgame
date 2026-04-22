// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuCOE/Nodes/CONodeModifierTransformWithBone.h"

#include "MuCOE/EdGraphSchema_CustomizableObject.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(CONodeModifierTransformWithBone)

#define LOCTEXT_NAMESPACE "CustomizableObjectEditor"

FText UCONodeModifierTransformWithBone::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return LOCTEXT("Transform_Mesh_In_Bone_Hierarchy", "Transform Mesh In Bone Hierarchy");
}

FText UCONodeModifierTransformWithBone::GetTooltipText() const
{
	return LOCTEXT("Transform_Mesh_In_Bone_Hierarchy_Tooltip", "Applies a transform to the vertices of a mesh that is skinned to the target bone or its child bones");
}

void UCONodeModifierTransformWithBone::AllocateDefaultPins(UCustomizableObjectNodeRemapPins* RemapPins)
{
	const UEdGraphSchema_CustomizableObject* Schema = GetDefault<UEdGraphSchema_CustomizableObject>();

	UEdGraphPin* NewTransformPin = CustomCreatePin(EGPD_Input, Schema->PC_Transform, FName("Transform"));
	NewTransformPin->bDefaultValueIsIgnored = true;
	TransformPin = NewTransformPin;

	UEdGraphPin* OutPin = CustomCreatePin(EGPD_Output, Schema->PC_Modifier, FName("Modifier"));
	OutPin->bDefaultValueIsIgnored = true;
	OutputPin = OutPin;

	//Create Node Modifier Common Pins
	Super::AllocateDefaultPins(RemapPins);
}


void UCONodeModifierTransformWithBone::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.Property == nullptr)
	{
		return;
	}
	
	if (PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UCONodeModifierTransformWithBone, BoundingMeshTransform)
		|| PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UCONodeModifierTransformWithBone, BoneName))
	{
		if (TransformChangedDelegate.IsBound())
		{
			TransformChangedDelegate.Broadcast(BoundingMeshTransform);
		}
	}
}


UEdGraphPin* UCONodeModifierTransformWithBone::GetOutputPin() const
{
	return OutputPin.Get();
}


UEdGraphPin* UCONodeModifierTransformWithBone::GetTransformPin() const
{
	return TransformPin.Get();
}

#undef LOCTEXT_NAMESPACE

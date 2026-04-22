// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuCOE/Nodes/CONodeMaterialConstant.h"
#include "MuCOE/CustomizableObjectGraph.h"
#include "MuCOE/EdGraphSchema_CustomizableObject.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(CONodeMaterialConstant)

class UCustomizableObjectNodeRemapPins;
struct FPropertyChangedEvent;
struct FSlateBrush;

#define LOCTEXT_NAMESPACE "CustomizableObjectEditor"

void UCONodeMaterialConstant::AllocateDefaultPins(UCustomizableObjectNodeRemapPins* RemapPins)
{
	const FName Type = UEdGraphSchema_CustomizableObject::PC_Material;
	const FName PinName = UEdGraphSchema_CustomizableObject::GetPinCategoryName(Type);
	const FText PinFriendlyName = UEdGraphSchema_CustomizableObject::GetPinCategoryFriendlyName(Type);
	
	UEdGraphPin* ValuePin = CustomCreatePin(EGPD_Output, Type, PinName);
	ValuePin->PinFriendlyName = PinFriendlyName;
}


FText UCONodeMaterialConstant::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return LOCTEXT("Material_Constant", "Material Constant");
}


FLinearColor UCONodeMaterialConstant::GetNodeTitleColor() const
{
	return UEdGraphSchema_CustomizableObject::GetPinTypeColor(UEdGraphSchema_CustomizableObject::PC_Material);
}


FText UCONodeMaterialConstant::GetTooltipText() const
{
	return LOCTEXT("Material_Constant_Tooltip", "Define an Unreal Engine Material that does not change at runtime.");
}

#undef LOCTEXT_NAMESPACE

// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuCOE/Nodes/CustomizableObjectNodeParameter.h"

#include "MuCO/CustomizableObjectCustomVersion.h"
#include "MuCOE/EdGraphSchema_CustomizableObject.h"
#include "MuCOE/CustomizableObjectMacroLibrary/CustomizableObjectMacroLibrary.h"
#include "MuCOE/GraphTraversal.h"
#include "MuCOE/Nodes/CustomizableObjectNodeMacroInstance.h"
#include "MuCOE/Nodes/CustomizableObjectNodeStaticString.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(CustomizableObjectNodeParameter)

class UCustomizableObjectNodeRemapPins;

#define LOCTEXT_NAMESPACE "CustomizableObjectEditor"


void UCustomizableObjectNodeParameter::AllocateDefaultPins(UCustomizableObjectNodeRemapPins* RemapPins)
{
	const FName Type = GetCategory();
	const FName PinName = UEdGraphSchema_CustomizableObject::GetPinCategoryName(Type);
	const FText PinFriendlyName = UEdGraphSchema_CustomizableObject::GetPinCategoryFriendlyName(Type);
	
	UEdGraphPin* ValuePin = CustomCreatePin(EGPD_Output, Type, PinName);
	ValuePin->PinFriendlyName = PinFriendlyName;
	ValuePin->bDefaultValueIsIgnored = true;

	NamePin = CustomCreatePin(EGPD_Input, UEdGraphSchema_CustomizableObject::PC_String, FName("Name"));
}


bool UCustomizableObjectNodeParameter::IsAffectedByLOD() const
{
	return false;
}


void UCustomizableObjectNodeParameter::BackwardsCompatibleFixup(int32 CustomizableObjectCustomVersion)
{
	Super::BackwardsCompatibleFixup(CustomizableObjectCustomVersion);

	if (CustomizableObjectCustomVersion == FCustomizableObjectCustomVersion::EnableMutableMacrosNewVersion)
	{
		if (!NamePin.Get())
		{
			NamePin = CustomCreatePin(EGPD_Input, UEdGraphSchema_CustomizableObject::PC_String, FName("Name"));
		}
	}
}


bool UCustomizableObjectNodeParameter::CanRenamePin(const UEdGraphPin& Pin) const
{
	return Pin.PinType.PinCategory == UEdGraphSchema_CustomizableObject::PC_String;
}


FText UCustomizableObjectNodeParameter::GetPinEditableName(const UEdGraphPin& Pin) const
{
	return FText::FromString(GetParameterName());
}


void UCustomizableObjectNodeParameter::SetPinEditableName(const UEdGraphPin& Pin, const FText& InValue)
{
	SetParameterName(InValue.ToString());
}


FText UCustomizableObjectNodeParameter::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	FFormatNamedArguments Args;
	Args.Add(TEXT("Type"), UEdGraphSchema_CustomizableObject::GetPinCategoryFriendlyName(GetCategory()));

	return FText::Format(LOCTEXT("ParameterTitle_ListView", "{Type} Parameter"), Args);
}


FLinearColor UCustomizableObjectNodeParameter::GetNodeTitleColor() const
{
	return UEdGraphSchema_CustomizableObject::GetPinTypeColor(GetCategory());
}


FText UCustomizableObjectNodeParameter::GetTooltipText() const
{
	return FText::Format(LOCTEXT("Parameter_Tooltip", "Expose a runtime modifiable {0} parameter from the Customizable Object."), FText::FromName(GetCategory()));
}


void UCustomizableObjectNodeParameter::OnRenameNode(const FString& NewName)
{
	if (!NewName.IsEmpty())
	{
		ParameterName = NewName;
	}
}


void UCustomizableObjectNodeParameter::PinConnectionListChanged(UEdGraphPin* Pin)
{
	if (Pin == NamePin.Get())
	{
		GetGraph()->NotifyGraphChanged();
	}
}


FString UCustomizableObjectNodeParameter::GetParameterName(TArray<const UCustomizableObjectNodeMacroInstance*>* MacroContext) const
{
	if (NamePin.Get())
	{
		if (const UEdGraphPin* LinkedPin = FollowInputPin(*NamePin.Get()))
		{
			const UEdGraphPin* StringPin = GraphTraversal::FindIOPinSourceThroughMacroContext(*LinkedPin, MacroContext);

			if (const UCustomizableObjectNodeStaticString* StringNode = StringPin ? Cast<UCustomizableObjectNodeStaticString>(StringPin->GetOwningNode()) : nullptr)
			{
				return StringNode->Value;
			}
		}
	}

	return ParameterName;
}


void UCustomizableObjectNodeParameter::SetParameterName(const FString& Name)
{
	ParameterName = Name;
}

#undef LOCTEXT_NAMESPACE


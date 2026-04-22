// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuCOE/Nodes/CustomizableObjectNodeComponentMeshAddTo.h"

#include "MuCO/CustomizableObjectCustomVersion.h"
#include "MuCOE/EdGraphSchema_CustomizableObject.h"
#include "MuCOE/GraphTraversal.h"
#include "MuCOE/Nodes/CustomizableObjectNodeComponentMesh.h"
#include "MuCOE/Nodes/CustomizableObjectNodeMacroInstance.h"
#include "MuCOE/Nodes/CustomizableObjectNodeStaticString.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(CustomizableObjectNodeComponentMeshAddTo)


#define LOCTEXT_NAMESPACE "CustomizableObjectEditor"

void UCustomizableObjectNodeComponentMeshAddTo::AllocateDefaultPins(UCustomizableObjectNodeRemapPins* RemapPins)
{
	Super::AllocateDefaultPins(RemapPins);

	LODPins.Empty(NumLODs);
	for (int32 LODIndex = 0; LODIndex < NumLODs; ++LODIndex)
	{
		FString LODName = FString::Printf(TEXT("LOD %d"), LODIndex);

		UEdGraphPin* Pin = CustomCreatePin(EGPD_Input, UEdGraphSchema_CustomizableObject::PC_MeshSection, FName(*LODName), true);
		LODPins.Add(Pin);
	}

	ParentComponentNamePin = CustomCreatePin(EGPD_Input, UEdGraphSchema_CustomizableObject::PC_String, FName("Component Name"));
	OutputPin = CustomCreatePin(EGPD_Output, UEdGraphSchema_CustomizableObject::PC_Component, FName(TEXT("Component")));
}


void UCustomizableObjectNodeComponentMeshAddTo::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FProperty* PropertyThatChanged = PropertyChangedEvent.Property;
	if (!PropertyThatChanged)
	{
		return;
	}

	if (PropertyThatChanged->GetFName() == GET_MEMBER_NAME_CHECKED(UCustomizableObjectNodeComponentMeshAddTo, NumLODs))
	{
		ReconstructNode();
	}
}


bool UCustomizableObjectNodeComponentMeshAddTo::IsAffectedByLOD() const
{
	return false;
}


bool UCustomizableObjectNodeComponentMeshAddTo::IsSingleOutputNode() const
{
	// todo UE-225446 : By limiting the number of connections this node can have we avoid a check failure. However, this method should be
	// removed in the future and the inherent issue with 1:n output connections should be fixed in its place
	return true;
}


void UCustomizableObjectNodeComponentMeshAddTo::BackwardsCompatibleFixup(int32 CustomizableObjectCustomVersion)
{
	Super::BackwardsCompatibleFixup(CustomizableObjectCustomVersion);

	if (CustomizableObjectCustomVersion == FCustomizableObjectCustomVersion::EnableMutableMacrosNewVersion)
	{
		if (!ParentComponentNamePin.Get())
		{
			ParentComponentNamePin = CustomCreatePin(EGPD_Input, UEdGraphSchema_CustomizableObject::PC_String, FName("Component Name"));
		}
	}
}


bool UCustomizableObjectNodeComponentMeshAddTo::CanRenamePin(const UEdGraphPin& Pin) const
{
	return Pin.PinType.PinCategory == UEdGraphSchema_CustomizableObject::PC_String;
}


FText UCustomizableObjectNodeComponentMeshAddTo::GetPinEditableName(const UEdGraphPin& Pin) const
{
	return FText::FromName(GetParentComponentName());
}


void UCustomizableObjectNodeComponentMeshAddTo::SetPinEditableName(const UEdGraphPin& Pin, const FText& InValue)
{
	SetParentComponentName(FName(*InValue.ToString()));
}


FText UCustomizableObjectNodeComponentMeshAddTo::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return LOCTEXT("ComponentMeshAdd", "Add To Skeletal Mesh Component");
}


FLinearColor UCustomizableObjectNodeComponentMeshAddTo::GetNodeTitleColor() const
{
	return UEdGraphSchema_CustomizableObject::GetPinTypeColor(UEdGraphSchema_CustomizableObject::PC_Component);
}


void UCustomizableObjectNodeComponentMeshAddTo::PinConnectionListChanged(UEdGraphPin* Pin)
{
	if (Pin == GetParentComponentNamePin())
	{
		GetGraph()->NotifyGraphChanged();
	}
}


int32 UCustomizableObjectNodeComponentMeshAddTo::GetNumLODs()
{
	return NumLODs;
}


ECustomizableObjectAutomaticLODStrategy UCustomizableObjectNodeComponentMeshAddTo::GetAutoLODStrategy()
{
	return AutoLODStrategy;
}


const TArray<FEdGraphPinReference>& UCustomizableObjectNodeComponentMeshAddTo::GetLODPins() const
{
	return LODPins;
}


UEdGraphPin* UCustomizableObjectNodeComponentMeshAddTo::GetOutputPin() const
{
	return OutputPin.Get();
}


void UCustomizableObjectNodeComponentMeshAddTo::SetOutputPin(const UEdGraphPin* Pin)
{
	OutputPin = Pin;
}


const UCustomizableObjectNode* UCustomizableObjectNodeComponentMeshAddTo::GetOwningNode() const
{
	return this;
}


FName UCustomizableObjectNodeComponentMeshAddTo::GetParentComponentName(TArray<const UCustomizableObjectNodeMacroInstance*>* MacroContext) const
{
	const UEdGraphPin* ParentNamePin = GetParentComponentNamePin();
	if (ParentNamePin)
	{
		if (const UEdGraphPin* LinkedPin = FollowInputPin(*ParentNamePin))
		{
			const UEdGraphPin* StringPin = GraphTraversal::FindIOPinSourceThroughMacroContext(*LinkedPin, MacroContext);

			if (const UCustomizableObjectNodeStaticString* StringNode = StringPin ? Cast<UCustomizableObjectNodeStaticString>(StringPin->GetOwningNode()) : nullptr)
			{
				return FName(StringNode->Value);
			}
		}
	}

	return ParentComponentName;
}


void UCustomizableObjectNodeComponentMeshAddTo::SetParentComponentName(const FName& InComponentName)
{
	ParentComponentName = InComponentName;
}


UEdGraphPin* UCustomizableObjectNodeComponentMeshAddTo::GetParentComponentNamePin() const
{
	return ParentComponentNamePin.Get();
}

#undef LOCTEXT_NAMESPACE


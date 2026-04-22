// Copyright Epic Games, Inc. All Rights Reserved.

#include "SCustomizableObjectNodePin.h"

#include "MuCOE/EdGraphSchema_CustomizableObject.h"
#include "MuCOE/UnrealEditorPortabilityHelpers.h"
#include "MuCOE/Nodes/CustomizableObjectNode.h"
#include "Widgets/Input/SEditableTextBox.h"


void SCustomizableObjectNodePin::Construct(const FArguments& InArgs, UEdGraphPin* InGraphPinObj)
{
	SGraphPin::Construct(SGraphPin::FArguments(), InGraphPinObj);

	// Cache pin icons.
	PassThroughImageConnected = UE_MUTABLE_GET_BRUSH(TEXT("Graph.ExecPin.Connected"));
	PassThroughImageDisconnected = UE_MUTABLE_GET_BRUSH(TEXT("Graph.ExecPin.Disconnected"));

	// Hide pin Icon and Label
	if (InGraphPinObj->bNotConnectable)
	{
		SetShowLabel(false);
		PinImage->SetVisibility(EVisibility::Collapsed);
	}
}


TSharedRef<SWidget> SCustomizableObjectNodePin::GetDefaultValueWidget()
{
	UEdGraphPin* GraphPin = GetPinObj();
	if (GraphPin && !GraphPin->IsPendingKill())
	{
		UCustomizableObjectNode* Node = CastChecked<UCustomizableObjectNode>(GetPinObj()->GetOwningNode());
		if (Node && Node->CanRenamePin(*GraphPin))
		{
			return SNew(SEditableTextBox)
				.Text(this, &SCustomizableObjectNodePin::GetNodeStringValue)
				.OnTextCommitted(this, &SCustomizableObjectNodePin::OnTextCommited)
				.Visibility(this, &SCustomizableObjectNodePin::GetWidgetVisibility);
		}	
	}

	return SGraphPin::GetDefaultValueWidget();
}


const FSlateBrush* SCustomizableObjectNodePin::GetPinIcon() const
{
	UEdGraphPin* GraphPin = GetPinObj();
	if (GraphPin && !GraphPin->IsPendingKill())
	{
		UCustomizableObjectNode* Node = CastChecked<UCustomizableObjectNode>(GetPinObj()->GetOwningNode());
		if (Node)
		{
			if (Node->IsPassthrough(*GraphPin))
			{
				if (GraphPinObj->LinkedTo.IsEmpty())
				{
					return PassThroughImageDisconnected;
				}
				else
				{
					return PassThroughImageConnected;
				}
			}
		}
	}
	
	return SGraphPin::GetPinIcon();
}


FSlateColor SCustomizableObjectNodePin::GetPinColor() const
{
	UEdGraphPin* GraphPin = GetPinObj();
	if (GraphPin && !GraphPin->IsPendingKill())
	{
		if (bIsDiffHighlighted)
		{
			return FSlateColor(FLinearColor(0.9f, 0.2f, 0.15f));
		}
		if (GraphPin->bOrphanedPin)
		{
			return FSlateColor(FLinearColor::Red);
		}
		
		UCustomizableObjectNode* Node = CastChecked<UCustomizableObjectNode>(GetPinObj()->GetOwningNode());
		if (Node)
		{
			const FLinearColor Color = Node->GetPinColor(*GraphPin);
			
			if (!Node->IsNodeEnabled() || Node->IsDisplayAsDisabledForced() || !IsEditingEnabled() || Node->IsNodeUnrelated())
			{
				return Color * FLinearColor(1.0f, 1.0f, 1.0f, 0.5f);
			}

			return Color * PinColorModifier;
		}
	}

	return FLinearColor::White;
}


FText SCustomizableObjectNodePin::GetNodeStringValue() const
{
	UEdGraphPin* GraphPin = GetPinObj();
	if (GraphPin && !GraphPin->IsPendingKill())
	{
		UCustomizableObjectNode* Node = CastChecked<UCustomizableObjectNode>(GetPinObj()->GetOwningNode());
		if (Node)
		{
			return Node->GetPinEditableName(*GraphPin);
		}
	}

	return {};
}


void SCustomizableObjectNodePin::OnTextCommited(const FText& InValue, ETextCommit::Type InCommitInfo)
{
	UEdGraphPin* GraphPin = GetPinObj();
	if (GraphPin && !GraphPin->IsPendingKill())
	{
		UCustomizableObjectNode* Node = CastChecked<UCustomizableObjectNode>(GetPinObj()->GetOwningNode());
		if (Node)
		{
			Node->SetPinEditableName(*GraphPin, InValue);
		}
	}
}


EVisibility SCustomizableObjectNodePin::GetWidgetVisibility() const
{
	return GraphPinObj->LinkedTo.Num() ? EVisibility::Collapsed : EVisibility::Visible;
}

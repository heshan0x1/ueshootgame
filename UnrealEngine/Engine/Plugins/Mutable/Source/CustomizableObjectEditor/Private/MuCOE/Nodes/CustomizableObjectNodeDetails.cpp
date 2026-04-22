// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuCOE/Nodes/CustomizableObjectNodeDetails.h"

#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "Input/Reply.h"
#include "MuCOE/Nodes/CustomizableObjectNode.h"
#include "MuCOE/PinViewer/SPinViewer.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Images/SImage.h"

class IDetailLayoutBuilder;

#define LOCTEXT_NAMESPACE "CustomizableObjectEditor"


TSharedRef<IDetailCustomization> FCustomizableObjectNodeDetails::MakeInstance()
{
	return MakeShareable(new FCustomizableObjectNodeDetails);
}


void FCustomizableObjectNodeDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TSharedPtr<const IDetailsView> DetailsView = DetailBuilder.GetDetailsViewSharedPtr();
	if (DetailsView && DetailsView->GetSelectedObjects().Num())
	{
		if (UCustomizableObjectNode* Node = Cast<UCustomizableObjectNode>(DetailsView->GetSelectedObjects()[0]))
		{
			if (!Node->HasPinViewer())
			{
				return;
			}
			
			IDetailCategoryBuilder& PinViewerCategoryBuilder = DetailBuilder.EditCategory("PinViewer", LOCTEXT("PinViewer", "Pins"), ECategoryPriority::Uncommon);
			
			if (Node->CanCreatePinsFromPinViewer())
			{
				FText TooltipText = LOCTEXT("NodeAddPinTooltip", "Create a new pin.");

				PinViewerCategoryBuilder.HeaderContent
				(
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.HAlign(HAlign_Right)
					[
						SNew(SButton)
						.ButtonStyle(FAppStyle::Get(), "SimpleButton")
						.ContentPadding(FMargin(1, 0))
						.OnClicked(this, &FCustomizableObjectNodeDetails::AddNewPin, Node)
						.HAlign(HAlign_Right)
						.ToolTipText(TooltipText)
						.VAlign(VAlign_Center)
						[
							SNew(SImage)
							.Image(FAppStyle::Get().GetBrush("Icons.PlusCircle"))
							.ColorAndOpacity(FSlateColor::UseForeground())
						]
					]
				);
			}

			PinViewerCategoryBuilder.AddCustomRow(LOCTEXT("PinViewerDetailsCategory", "PinViewer")).ShouldAutoExpand(true)
			[
				SNew(SPinViewer).Node(Node)
			];
		}
	}
}


// Only implemented to silence -Woverloaded-virtual warning.
void FCustomizableObjectNodeDetails::CustomizeDetails(const TSharedPtr<IDetailLayoutBuilder>& DetailBuilder)
{
	IDetailCustomization::CustomizeDetails(DetailBuilder);

	DetailBuilderPtr = DetailBuilder;
}


FReply FCustomizableObjectNodeDetails::AddNewPin(UCustomizableObjectNode* Node)
{
	if (Node && DetailBuilderPtr)
	{
		Node->CreatePinFromPinViewer();
		DetailBuilderPtr->ForceRefreshDetails();

		return FReply::Handled();
	}

	return FReply::Unhandled();
}

#undef LOCTEXT_NAMESPACE

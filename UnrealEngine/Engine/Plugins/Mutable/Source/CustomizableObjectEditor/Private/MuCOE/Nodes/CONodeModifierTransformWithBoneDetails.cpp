// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuCOE/Nodes/CONodeModifierTransformWithBoneDetails.h"

#include "MuCOE/Nodes/CONodeModifierTransformWithBone.h"

#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"

#define LOCTEXT_NAMESPACE "CONodeModifierTransformWithMeshDetails"


TSharedRef<IDetailCustomization> FCONodeModifierTransformWithBoneDetails::MakeInstance()
{
	return MakeShareable(new FCONodeModifierTransformWithBoneDetails);
}


void FCONodeModifierTransformWithBoneDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	FCustomizableObjectNodeModifierBaseDetails::CustomizeDetails(DetailBuilder);

	Node = nullptr;
	DetailBuilderPtr = &DetailBuilder;

	TSharedPtr<const IDetailsView> DetailsView = DetailBuilder.GetDetailsViewSharedPtr();
	if (DetailsView && DetailsView->GetSelectedObjects().Num())
	{
		Node = Cast<UCONodeModifierTransformWithBone>(DetailsView->GetSelectedObjects()[0].Get());
	}

	IDetailCategoryBuilder& CustomizableObjectToClipCategory = DetailBuilder.EditCategory("Transform");

	// TODO Remove
	if(!Node)
	{
		CustomizableObjectToClipCategory.AddCustomRow(LOCTEXT("Node", "Node"))
			[
				SNew(STextBlock)
				.Text(LOCTEXT("Node not found", "Node not found"))
			];
	}
}

#undef LOCTEXT_NAMESPACE

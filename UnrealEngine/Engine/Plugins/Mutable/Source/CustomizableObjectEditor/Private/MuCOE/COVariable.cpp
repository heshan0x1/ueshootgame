// Copyright Epic Games, Inc. All Rights Reserved.

#include "COVariable.h"

#include "DetailLayoutBuilder.h"
#include "MuCOE/EdGraphSchema_CustomizableObject.h"
#include "MuCOE/Nodes/CustomizableObjectNode.h"
#include "IDetailsView.h"
#include "PropertyCustomizationHelpers.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/SWidget.h"


TSharedRef<IPropertyTypeCustomization> FCOVariableCustomzation::MakeInstance()
{
	return MakeShared<FCOVariableCustomzation>();
}


void FCOVariableCustomzation::CustomizeHeader(TSharedRef<IPropertyHandle> StructPropertyHandle, FDetailWidgetRow& InHeaderRow, IPropertyTypeCustomizationUtils& InCustomizationUtils)
{
	// Get the property handles to edit the variable values
	NamePropertyHandle = StructPropertyHandle->GetChildHandle("Name");
	TypePropertyHandle = StructPropertyHandle->GetChildHandle("Type");
	check(NamePropertyHandle && TypePropertyHandle);

	//Get parent node if it's defined inside one
	TArray<UObject*> OuterObjects;
	StructPropertyHandle->GetOuterObjects(OuterObjects);

	if (OuterObjects.Num())
	{
		BaseObjectNode = Cast<UCustomizableObjectNode>(OuterObjects[0]->GetTypedOuter(UCustomizableObjectNode::StaticClass()));

		//You can add more types here. This class is not meant to work just with nodes.
	}

	// Get which type of pins we can create
	if (BaseObjectNode.IsValid())
	{
		AllowedVariableTypes = BaseObjectNode->GetAllowedPinViewerCreationTypes();
	}
	else
	{
		AllowedVariableTypes = UEdGraphSchema_CustomizableObject::SupportedMacroPinTypes;
	}

	InHeaderRow.NameContent()
	[
		SNew(SEditableTextBox)
		.Text(this, &FCOVariableCustomzation::GetVariableName)
		.OnTextChanged(this, &FCOVariableCustomzation::OnVariableNameChanged)
		.OnTextCommitted(this, &FCOVariableCustomzation::OnVariableNameCommitted)
		.Font(IDetailLayoutBuilder::GetDetailFont())
	]
	.ValueContent()
	[
		// Combo button that summons the dropdown menu
		SNew(SComboBox<FName>)
		.OptionsSource(&AllowedVariableTypes)
		.OnGenerateWidget(this, &FCOVariableCustomzation::OnGenerateVariableTypeRow)
		.OnSelectionChanged_Lambda
		([this](FName Item, ESelectInfo::Type)
			{
				TypePropertyHandle->SetValue(Item);

				if (BaseObjectNode.IsValid())
				{
					BaseObjectNode->ReconstructNode();
				}
			}
		)
		.Content()
		[
			GenerateCurrentSelectectedTypeWidget()
		]
	]
	.OverrideResetToDefault(FResetToDefaultOverride::Create(FSimpleDelegate::CreateSP(this, &FCOVariableCustomzation::ResetToDefault)));
}


FText FCOVariableCustomzation::GetVariableName() const
{
	FName VariableName;
	NamePropertyHandle->GetValue(VariableName);

	return FText::FromName(VariableName);
}


void FCOVariableCustomzation::ResetToDefault()
{
	NamePropertyHandle->SetValue(FName("NewVar"));
	TypePropertyHandle->SetValue(AllowedVariableTypes.Num() ? AllowedVariableTypes[0] : NAME_None);
}


void FCOVariableCustomzation::OnVariableNameChanged(const FText& InNewText)
{
	FName NewName = FName(InNewText.ToString());

	if (InNewText.IsEmpty() || NewName.IsNone())
	{
		return;
	}

	NamePropertyHandle->SetValue(NewName);
}


void FCOVariableCustomzation::OnVariableNameCommitted(const FText& InNewText, ETextCommit::Type InTextCommit)
{
	FName NewName = FName(InNewText.ToString());

	if (InNewText.IsEmpty() || NewName.IsNone())
	{
		return;
	}

	NamePropertyHandle->SetValue(NewName);

	if (BaseObjectNode.IsValid())
	{
		BaseObjectNode->ReconstructNode();
	}
}


TSharedRef<SWidget> FCOVariableCustomzation::OnGenerateVariableTypeRow(FName Type)
{
	const FSlateBrush* IconBrush = FAppStyle::GetBrush(TEXT("Kismet.VariableList.TypeIcon"));

	TSharedPtr<SHorizontalBox> RowWidget = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			SNew(SImage)
				.Image(IconBrush)
				.ColorAndOpacity(UEdGraphSchema_CustomizableObject::GetPinTypeColor(Type))
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(7.5f, 0.0f, 0.0f, 0.0f)
		[
			SNew(STextBlock)
				.Text(Type.IsValid() ? GetDefault<UEdGraphSchema_CustomizableObject>()->GetPinCategoryFriendlyName(Type) : FText::FromString("Invalid"))
				.Font(IDetailLayoutBuilder::GetDetailFont())
		];

	return RowWidget.ToSharedRef();
}


TSharedRef<SWidget> FCOVariableCustomzation::GenerateCurrentSelectectedTypeWidget()
{
	FName CurrentType;
	TypePropertyHandle->GetValue(CurrentType);

	return OnGenerateVariableTypeRow(CurrentType);
}

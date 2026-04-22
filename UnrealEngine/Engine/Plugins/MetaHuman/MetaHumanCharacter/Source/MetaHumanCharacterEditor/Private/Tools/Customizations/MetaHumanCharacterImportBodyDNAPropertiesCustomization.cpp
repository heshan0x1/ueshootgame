// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tools/Customizations/MetaHumanCharacterImportBodyDNAPropertiesCustomization.h"
#include "Tools/MetaHumanCharacterEditorBodyConformTool.h"
#include "MetaHumanCharacterEditorStyle.h"

#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "PropertyHandle.h"
#include "DetailWidgetRow.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SButton.h"

#define LOCTEXT_NAMESPACE "MetaHumanCharacterEditor"

TSharedRef<IDetailCustomization> FMetaHumanCharacterImportBodyDNAPropertiesCustomization::MakeInstance()
{
	return MakeShareable(new FMetaHumanCharacterImportBodyDNAPropertiesCustomization);
}

void FMetaHumanCharacterImportBodyDNAPropertiesCustomization::CustomizeDetails(IDetailLayoutBuilder& InDetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> Objects;
	InDetailBuilder.GetObjectsBeingCustomized(Objects);

	UObject* SelectedObject = Objects.Num() > 0 ? Objects[0].Get() : nullptr;
	if (!SelectedObject || !IsValid(SelectedObject))
	{
		return;
	}

	UMetaHumanCharacterImportBodyDNAProperties* ImportBodyDNAProperties = Cast<UMetaHumanCharacterImportBodyDNAProperties>(SelectedObject);

	if (!ImportBodyDNAProperties)
	{
		return;
	}

	TSharedPtr<IPropertyHandle> ImportBodyFromDNAParamsHandle = InDetailBuilder.GetProperty("ImportOptions");
	if (!ImportBodyFromDNAParamsHandle || !ImportBodyFromDNAParamsHandle->IsValidHandle())
	{
		return;
	}
	
	TSharedPtr<IPropertyHandle> TargetIsInAPoseHandle = ImportBodyFromDNAParamsHandle->GetChildHandle("bTargetIsInMetaHumanAPose");
	if (!TargetIsInAPoseHandle || !TargetIsInAPoseHandle->IsValidHandle())
	{
		return;
	}
	InDetailBuilder.HideProperty(TargetIsInAPoseHandle);

	TSharedPtr<IPropertyHandle> EstimateJointsFromMeshHandle = ImportBodyFromDNAParamsHandle->GetChildHandle("bEstimateJointsFromMesh");
	if (!EstimateJointsFromMeshHandle || !EstimateJointsFromMeshHandle->IsValidHandle())
	{
		return;
	}

	int32 SortOrder = 10;
	IDetailCategoryBuilder& FileCategory = InDetailBuilder.EditCategory("File", LOCTEXT("CategoryFile", "File"), ECategoryPriority::Important);
	FileCategory.SetSortOrder(SortOrder);

	auto AddButtonRow = [ImportBodyDNAProperties](IDetailCategoryBuilder& CategoryToUpdate, const FText& ButtonText, const FText& TooltipText, TAttribute<bool>&& IsEnabledAttr, TFunction<void()>&& OnClicked)
		{
			CategoryToUpdate.AddCustomRow(FText::GetEmpty())
			.WholeRowContent()
			[
				SNew(SBorder)
				.BorderImage(FMetaHumanCharacterEditorStyle::Get().GetBrush("MetaHumanCharacterEditorTools.ActiveToolLabel"))
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(SBox)
						.HeightOverride(50.f)
						.HAlign(HAlign_Fill)
						.Padding(10.f)
						[
							SNew(SButton)
							.ButtonStyle(FAppStyle::Get(), "FlatButton.Success")
							.ForegroundColor(FLinearColor::White)
							.IsEnabled(MoveTemp(IsEnabledAttr))
							.ToolTipText(TooltipText)
							.OnClicked(FOnClicked::CreateWeakLambda(ImportBodyDNAProperties, [Fun = MoveTemp(OnClicked)]()
								{
									Fun();
									return FReply::Handled();
								}))
							.HAlign(HAlign_Center)
							.VAlign(VAlign_Center)
							[
								SNew(STextBlock)
								.Text(ButtonText)
								.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
							]
						]
					]
				]
			];
		};


	// Conform category
	{
		IDetailCategoryBuilder& Category = InDetailBuilder.EditCategory("Conform", LOCTEXT("CategoryConform", "Conform"));
		Category.SetSortOrder(++SortOrder);

		AddButtonRow(
			Category,
			LOCTEXT("ButtonDNAConform", "Conform"),
			LOCTEXT("ButtonDNAConformTooltip", "Auto-Rig the source meshes. Joints, RBFs and skin weights will be automatically generated to fit the mesh."),
			TAttribute<bool>::Create(TAttribute<bool>::FGetter::CreateUObject(ImportBodyDNAProperties, &UMetaHumanCharacterImportBodyDNAProperties::CanConform)),
			[ImportBodyDNAProperties]()
			{
				ImportBodyDNAProperties->Conform();
			});
	}

	// Import Mesh category
	{
		IDetailCategoryBuilder& Category = InDetailBuilder.EditCategory("Import Mesh", LOCTEXT("CategoryImportMesh", "Import Mesh"));
		Category.SetSortOrder(++SortOrder);

		AddButtonRow(
			Category,
			LOCTEXT("ButtonDNAImportMesh", "Import Mesh"),
			LOCTEXT("ButtonDNAImportMeshTooltip", "Import mesh from the source DNA. Core joints will remain unchanged. Helper joints and RBFs can be (optionally) re-fitted to the new mesh."),
			TAttribute<bool>::Create(TAttribute<bool>::FGetter::CreateUObject(ImportBodyDNAProperties, &UMetaHumanCharacterImportBodyDNAProperties::CanImportMesh)),
			[ImportBodyDNAProperties]()
			{
				ImportBodyDNAProperties->ImportMesh();
			}
		);
	}

	// Import Joints category
	{
		IDetailCategoryBuilder& Category = InDetailBuilder.EditCategory("Import Joints", LOCTEXT("CategoryImportJoints", "Import Joints"));
		Category.SetSortOrder(++SortOrder);

		AddButtonRow(
			Category,
			LOCTEXT("ButtonDNAImportJoints", "Import Joints"),
			LOCTEXT("ButtonDNAImportJointsTooltip", "Import Core joints and (optionally) helper joints from the source DNA. The mesh will remain unchanged."),
			TAttribute<bool>::Create(TAttribute<bool>::FGetter::CreateUObject(ImportBodyDNAProperties, &UMetaHumanCharacterImportBodyDNAProperties::CanImportJoints)),
			[ImportBodyDNAProperties]()
			{
				ImportBodyDNAProperties->ImportJoints();
			});
	}

	// Import Whole Rig category
	{
		IDetailCategoryBuilder& Category = InDetailBuilder.EditCategory("Import Whole Rig", LOCTEXT("CategoryImportDNA", "Import Whole Rig"));
		Category.InitiallyCollapsed(false);
		AddButtonRow(
			Category,
			LOCTEXT("ButtonDNAImportWholeRig", "Import Whole Rig"),
			LOCTEXT("ButtonDNAImportWholeRigTooltip", "Imports mesh, joints, skin weights and RBFs from DNA resulting in a fixed, non-editable body type."),
			TAttribute<bool>::Create(TAttribute<bool>::FGetter::CreateUObject(ImportBodyDNAProperties, &UMetaHumanCharacterImportBodyDNAProperties::CanImportWholeRig)),
			[ImportBodyDNAProperties]()
			{
				ImportBodyDNAProperties->ImportWholeRig();
			});
	}
}

#undef LOCTEXT_NAMESPACE

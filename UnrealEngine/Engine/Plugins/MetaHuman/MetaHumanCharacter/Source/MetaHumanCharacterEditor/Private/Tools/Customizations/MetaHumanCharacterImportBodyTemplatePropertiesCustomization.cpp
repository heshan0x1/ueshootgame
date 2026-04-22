// Copyright Epic Games, Inc. All Rights Reserved.
#include "MetaHumanCharacterImportBodyTemplatePropertiesCustomization.h"

#include "Tools/Customizations/MetaHumanCharacterImportTemplatePropertiesCustomization.h"
#include "Tools/MetaHumanCharacterEditorBodyConformTool.h"
#include "MetaHumanCharacterEditorStyle.h"

#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "PropertyHandle.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Engine/StaticMesh.h" 
#include "Engine/SkeletalMesh.h" 
#include "DetailWidgetRow.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SButton.h"

#define LOCTEXT_NAMESPACE "MetaHumanCharacterEditor"

TSharedRef<IDetailCustomization> FMetaHumanCharacterImportBodyTemplatePropertiesCustomization::MakeInstance()
{
	return MakeShareable(new FMetaHumanCharacterImportBodyTemplatePropertiesCustomization);
}

void FMetaHumanCharacterImportBodyTemplatePropertiesCustomization::CustomizeDetails(IDetailLayoutBuilder& InDetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> Objects;
	InDetailBuilder.GetObjectsBeingCustomized(Objects);

	UObject* SelectedObject = Objects.Num() > 0 ? Objects[0].Get() : nullptr;
	if (!SelectedObject || !IsValid(SelectedObject))
	{
		return;
	}
	UMetaHumanCharacterImportBodyTemplateProperties* ImportBodyTemplateProperties = Cast<UMetaHumanCharacterImportBodyTemplateProperties>(SelectedObject);

	TSharedPtr<IPropertyHandle> ConformBodyParamsHandle =
	   InDetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UMetaHumanCharacterImportBodyTemplateProperties, ConformBodyParams));

	
	TSharedPtr<IPropertyHandle> TargetIsInAPoseHandle = ConformBodyParamsHandle->GetChildHandle("bTargetIsInMetaHumanAPose");
	if (!TargetIsInAPoseHandle || !TargetIsInAPoseHandle->IsValidHandle())
	{
		return;
	}

	TSharedPtr<IPropertyHandle> BodyMeshHandle = InDetailBuilder.GetProperty("BodyMesh");
	if (!BodyMeshHandle || !BodyMeshHandle->IsValidHandle())
	{
		return;
	}
	
	FProperty* BodyMeshProperty = SelectedObject->GetClass()->FindPropertyByName("BodyMesh");
	if (!BodyMeshProperty)
	{
		return;
	}
	InDetailBuilder.HideProperty(TargetIsInAPoseHandle);

	int32 SortOrder = 10;
	IDetailCategoryBuilder& FileCategory = InDetailBuilder.EditCategory("Asset", LOCTEXT("CategoryAsset", "Asset"), ECategoryPriority::Important);
	FileCategory.SetSortOrder(SortOrder);

	auto AddButtonRow = [ImportBodyTemplateProperties](IDetailCategoryBuilder& CategoryToUpdate, const FText& ButtonText, const FText& TooltipText, TAttribute<bool>&& IsEnabledAttr, TFunction<void()>&& OnClicked)
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
							.OnClicked(FOnClicked::CreateWeakLambda(ImportBodyTemplateProperties, [Fun = MoveTemp(OnClicked)]()
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

	auto StaticMeshVisibilityLambda = [BodyMeshProperty, BodyMeshHandle]()
		{
			if (CastField<FSoftObjectProperty>(BodyMeshProperty)) // check it is a soft object property first
			{
				// get the type of mesh from the property handle. Because this is a soft object ptr, we need to use the asset registry as
				// it won't necessarily be loaded / resolved
				void* AssetPtr;
				if (BodyMeshHandle->GetValueData(AssetPtr) == FPropertyAccess::Success)
				{
					TSoftObjectPtr<UObject>* MeshSoftObjectPtr = static_cast<TSoftObjectPtr<UObject>*>(AssetPtr); // we need an explicit cast from the value data 

					if (MeshSoftObjectPtr)
					{
						// Get the asset path and look for it in the asset registry
						FSoftObjectPath AssetPath = MeshSoftObjectPtr->ToString();
						FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
						FAssetData AssetData = AssetRegistryModule.Get().GetAssetByObjectPath(AssetPath);

						if (AssetData.IsValid() && AssetData.GetClass()->IsChildOf(UStaticMesh::StaticClass()))
						{
							return EVisibility::Visible;;
						}
					}

				}

			}

			return EVisibility::Hidden;
		};

	 
	// Conform category
	{
		IDetailCategoryBuilder& Category = InDetailBuilder.EditCategory("Conform", LOCTEXT("CategoryConform", "Conform"));
		Category.AddProperty(TargetIsInAPoseHandle).Visibility(TAttribute<EVisibility>::Create(TAttribute<EVisibility>::FGetter::CreateLambda(StaticMeshVisibilityLambda)));
		Category.SetSortOrder(++SortOrder);
		AddButtonRow(
			Category,
			LOCTEXT("ButtonConform", "Conform"),
			LOCTEXT("ButtonConformTooltip", "Auto-Rig the source meshes. Joints, RBFs and skin weights will be automatically generated to fit the mesh."),
			TAttribute<bool>::Create(TAttribute<bool>::FGetter::CreateUObject(ImportBodyTemplateProperties, &UMetaHumanCharacterImportBodyTemplateProperties::CanConform)),
			[ImportBodyTemplateProperties]()
			{
				ImportBodyTemplateProperties->Conform();
			});
	}

	// Import Mesh category
	{
		IDetailCategoryBuilder& Category = InDetailBuilder.EditCategory("Import Mesh", LOCTEXT("CategoryImportMesh", "Import Mesh"));
		Category.SetSortOrder(++SortOrder);
		AddButtonRow(
			Category,
			LOCTEXT("ButtonImportMesh", "Import Mesh"),
			LOCTEXT("ButtonImportMeshTooltip", "Import mesh from the source mesh. Core joints will remain unchanged. Helper joints and RBFs can be (optionally) re-fitted to the new mesh."),
			TAttribute<bool>::Create(TAttribute<bool>::FGetter::CreateUObject(ImportBodyTemplateProperties, &UMetaHumanCharacterImportBodyTemplateProperties::CanImportMesh)),
			[ImportBodyTemplateProperties]()
			{
				ImportBodyTemplateProperties->ImportMesh();
			}
		);
	}

	// Import Joints category
	{
		IDetailCategoryBuilder& Category = InDetailBuilder.EditCategory("Import Joints", LOCTEXT("CategoryImportJoints", "Import Joints"));
		Category.SetSortOrder(++SortOrder);
		AddButtonRow(
			Category,
			LOCTEXT("ButtonImportJoints", "Import Joints"),
			LOCTEXT("ButtonImportJointsTooltip", "Import Core joints and (optionally) helper joints from the source body skeletal mesh asset. The mesh will remain unchanged."),
			TAttribute<bool>::Create(TAttribute<bool>::FGetter::CreateUObject(ImportBodyTemplateProperties, &UMetaHumanCharacterImportBodyTemplateProperties::CanImportJoints)),
			[ImportBodyTemplateProperties]()
			{
				ImportBodyTemplateProperties->ImportJoints();
			});
	}


}

#undef LOCTEXT_NAMESPACE
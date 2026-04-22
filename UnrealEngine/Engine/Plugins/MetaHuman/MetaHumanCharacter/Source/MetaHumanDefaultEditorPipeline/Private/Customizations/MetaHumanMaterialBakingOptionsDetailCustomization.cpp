// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanMaterialBakingOptionsDetailCustomization.h"
#include "MetaHumanDefaultEditorPipelineBase.h"

#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "IPropertyUtilities.h"
#include "ScopedTransaction.h"
#include "Widgets/Input/SButton.h"

#define LOCTEXT_NAMESPACE "MetaHumanMaterialBakingOptionsDetailsCustomizations"

TSharedRef<IPropertyTypeCustomization> FMetaHumanMaterialBakingOptionsDetailCustomziation::MakeInstance()
{
	return MakeShared<FMetaHumanMaterialBakingOptionsDetailCustomziation>();
}

void FMetaHumanMaterialBakingOptionsDetailCustomziation::CustomizeHeader(TSharedRef<IPropertyHandle> InPropertyHandle, FDetailWidgetRow& InHeaderRow, IPropertyTypeCustomizationUtils& InCustomizationUtils)
{
	InHeaderRow.NameContent()
	[
		InPropertyHandle->CreatePropertyNameWidget()
	]
	.ValueContent()
	[
		InPropertyHandle->CreatePropertyValueWidget()
	];
}

void FMetaHumanMaterialBakingOptionsDetailCustomziation::CustomizeChildren(TSharedRef<IPropertyHandle> InPropertyHandle, IDetailChildrenBuilder& InChildBuilder, IPropertyTypeCustomizationUtils& InCustomizationUtils)
{
	TSharedRef<IPropertyHandle> BakingSettingsProperty = InPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FMetaHumanMaterialBakingOptions, BakingSettings)).ToSharedRef();
	TSharedRef<IPropertyHandle> TextureResolutionsOverridesProperty = InPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FMetaHumanMaterialBakingOptions, TextureResolutionsOverrides)).ToSharedRef();

	// Add all children to the builder but customize TextureResolutionsOverridesProperty
	uint32 NumChildren = 0;
	if (InPropertyHandle->GetNumChildren(NumChildren) == FPropertyAccess::Success)
	{
		for (uint32 Index = 0; Index < NumChildren; ++Index)
		{
			if (TSharedPtr<IPropertyHandle> ChildProperty = InPropertyHandle->GetChildHandle(Index))
			{
				if (ChildProperty->IsSamePropertyNode(TextureResolutionsOverridesProperty))
				{
					const bool bShowChildren = true;
					InChildBuilder.AddProperty(ChildProperty.ToSharedRef())
						.CustomWidget(bShowChildren)
						.NameContent()
						[
							ChildProperty->CreatePropertyNameWidget()
						]
						.ValueContent()
						[
							SNew(SButton)
								.Text(LOCTEXT("RefreshList", "Refresh"))
								.ToolTipText(LOCTEXT("RefreshListTooltip", "Refreshes the list of resolution overrides based on the output textures defined in"
																		   " Baking Settings. This action removes entries that are not defined in Baking Settings"
																		   " and adds any new ones found"))
								.OnClicked_Lambda([InPropertyHandle]
												  {
													  void* StructData = nullptr;
													  if (InPropertyHandle->GetValueData(StructData) == FPropertyAccess::Success)
													  {
														  FMetaHumanMaterialBakingOptions* BakingOptions = (FMetaHumanMaterialBakingOptions*) StructData;

														  TArray<UObject*> Objects;
														  InPropertyHandle->GetOuterObjects(Objects);

														  const FScopedTransaction Transation(LOCTEXT("RefreshListTransaction", "Refresh Texture Resolution Overrides"));

														  if (!Objects.IsEmpty())
														  {
															  // Allows undoing the changes to the list of resolution overrides
															  Objects[0]->Modify();
														  }

														  BakingOptions->RefreshTextureResolutionsOverrides();
													  }
													  
													  return FReply::Handled();
												  })
								.IsEnabled_Lambda([InPropertyHandle]
												  {
													  bool bIsEnabled = false;

													  void* StructData = nullptr;
													  if (InPropertyHandle->GetValueData(StructData) == FPropertyAccess::Success)
													  {
														  FMetaHumanMaterialBakingOptions* BakingOptions = (FMetaHumanMaterialBakingOptions*) StructData;
														  bIsEnabled = !BakingOptions->BakingSettings.IsNull();
													  }

													  return bIsEnabled;
												  })
						];
				}
				else
				{
					if (ChildProperty->IsSamePropertyNode(BakingSettingsProperty))
					{
						TArray<UObject*> OwnerObjects;
						InPropertyHandle->GetOuterObjects(OwnerObjects);

						const bool bIsCDOOrArchetype = !OwnerObjects.IsEmpty() && OwnerObjects[0]->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject);
						if (!bIsCDOOrArchetype)
						{
							// Skip the Bake Settings property is being customized inside a tool
							continue;
						}
					}

					InChildBuilder.AddProperty(ChildProperty.ToSharedRef());
				}
			}
		}
	}
	
}

#undef LOCTEXT_NAMESPACE
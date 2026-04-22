// Copyright Epic Games, Inc. All Rights Reserved.

#include "CompositeLayerSingleLightShadowCustomization.h"

#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "IDetailGroup.h"
#include "IPropertyUtilities.h"
#include "Layers/CompositeLayerSingleLightShadow.h"
#include "UI/SCompositeActorPickerTable.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "FCompositeLayerSingleLightShadowCustomization"

TSharedRef<IDetailCustomization> FCompositeLayerSingleLightShadowCustomization::MakeInstance()
{
	return MakeShared<FCompositeLayerSingleLightShadowCustomization>();
}

void FCompositeLayerSingleLightShadowCustomization::CustomizeDetails(const TSharedPtr<IDetailLayoutBuilder>& DetailBuilder)
{
	CachedDetailBuilder = DetailBuilder;
	IDetailCustomization::CustomizeDetails(DetailBuilder);
}

void FCompositeLayerSingleLightShadowCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailLayout)
{
	DetailLayout.HideProperty(GET_MEMBER_NAME_CHECKED(UCompositeLayerSingleLightShadow, ShadowCastingActors));
	
	TArray<TWeakObjectPtr<UCompositeLayerSingleLightShadow>> Objects = DetailLayout.GetObjectsOfTypeBeingCustomized<UCompositeLayerSingleLightShadow>();

	IDetailCategoryBuilder& LayerCategory = DetailLayout.EditCategoryAllowNone(NAME_None);
	
	// Add all default simple properties ahead of the custom group
	TArray<TSharedRef<IPropertyHandle>> LayerProperties;
	LayerCategory.GetDefaultProperties(LayerProperties, /* bSimpleProperties */ true, /* bAdvancedProperties */ false);
	for (const TSharedRef<IPropertyHandle>& Property : LayerProperties)
	{
		if (!Property->IsCustomized())
		{
			LayerCategory.AddProperty(Property);
		}
	}

	if (Objects.Num() == 1 && Objects[0].IsValid())
	{
		UCompositeLayerSingleLightShadow* SingleLightShadow = Cast<UCompositeLayerSingleLightShadow>(Objects[0].Get());
		
		IDetailGroup& ActorListGroup = LayerCategory.AddGroup("ActorContent", LOCTEXT("ActorListGroupName", "Shadow Casting Content"), false, true);
		FCompositeActorPickerListRef ActorListRef(SingleLightShadow, GET_MEMBER_NAME_CHECKED(UCompositeLayerSingleLightShadow, ShadowCastingActors), &SingleLightShadow->ShadowCastingActors);
		
		ActorListGroup.AddWidgetRow()
		.WholeRowContent()
		[
			SNew(SCompositeActorPickerTable, ActorListRef)
			.OnLayoutSizeChanged(this, &FCompositeLayerSingleLightShadowCustomization::OnLayoutSizeChanged)
		];
	}
	else
	{
		// Can't display actor list if multiple layers are selected, so simply put a "Multiple Values" entry in the property list
		LayerCategory.AddCustomRow(LOCTEXT("ActorListGroupName", "Shadow Casting Content"))
		.NameContent()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("ActorListGroupName", "Shadow Casting Content"))
			.Font(IDetailLayoutBuilder::GetDetailFont())
		]
		.ValueContent()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("MultipleValues", "Multiple Values"))
			.Font(IDetailLayoutBuilder::GetDetailFont())
		];
	}
}

void FCompositeLayerSingleLightShadowCustomization::OnLayoutSizeChanged()
{
	TSharedPtr<IDetailLayoutBuilder> PinnedDetailBuilder = CachedDetailBuilder.Pin();
	if (PinnedDetailBuilder.IsValid())
	{
		PinnedDetailBuilder->GetPropertyUtilities()->RequestRefresh();
	}
}

#undef LOCTEXT_NAMESPACE

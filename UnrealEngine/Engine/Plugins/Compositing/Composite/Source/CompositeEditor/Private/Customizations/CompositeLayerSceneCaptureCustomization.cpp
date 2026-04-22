// Copyright Epic Games, Inc. All Rights Reserved.

#include "CompositeLayerSceneCaptureCustomization.h"

#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "IDetailGroup.h"
#include "IPropertyUtilities.h"
#include "Layers/CompositeLayerSceneCapture.h"
#include "UI/SCompositeActorPickerTable.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "FCompositeLayerSceneCaptureCustomization"

TSharedRef<IDetailCustomization> FCompositeLayerSceneCaptureCustomization::MakeInstance()
{
	return MakeShared<FCompositeLayerSceneCaptureCustomization>();
}

void FCompositeLayerSceneCaptureCustomization::CustomizeDetails(const TSharedPtr<IDetailLayoutBuilder>& DetailBuilder)
{
	CachedDetailBuilder = DetailBuilder;
	IDetailCustomization::CustomizeDetails(DetailBuilder);
}

void FCompositeLayerSceneCaptureCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailLayout)
{
	DetailLayout.HideProperty(GET_MEMBER_NAME_CHECKED(UCompositeLayerSceneCapture, Actors));
	
	TArray<TWeakObjectPtr<UCompositeLayerSceneCapture>> Objects = DetailLayout.GetObjectsOfTypeBeingCustomized<UCompositeLayerSceneCapture>();

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
		UCompositeLayerSceneCapture* SceneCapture = Cast<UCompositeLayerSceneCapture>(Objects[0].Get());
		
		IDetailGroup& ActorListGroup = LayerCategory.AddGroup("ActorContent", LOCTEXT("ActorListGroupName", "Scene Capture Content"), false, true);
		FCompositeActorPickerListRef ActorListRef(SceneCapture, GET_MEMBER_NAME_CHECKED(UCompositeLayerSceneCapture, Actors), &SceneCapture->Actors);
		
		ActorListGroup.AddWidgetRow()
		.WholeRowContent()
		[
			SNew(SCompositeActorPickerTable, ActorListRef)
			.OnLayoutSizeChanged(this, &FCompositeLayerSceneCaptureCustomization::OnLayoutSizeChanged)
		];
	}
	else
	{
		// Can't display actor list if multiple layers are selected, so simply put a "Multiple Values" entry in the property list
		LayerCategory.AddCustomRow(LOCTEXT("ActorListGroupName", "Scene Capture Content"))
		.NameContent()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("ActorListGroupName", "Scene Capture Content"))
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

void FCompositeLayerSceneCaptureCustomization::OnLayoutSizeChanged()
{
	TSharedPtr<IDetailLayoutBuilder> PinnedDetailBuilder = CachedDetailBuilder.Pin();
	if (PinnedDetailBuilder.IsValid())
	{
		PinnedDetailBuilder->GetPropertyUtilities()->RequestRefresh();
	}
}

#undef LOCTEXT_NAMESPACE

// Copyright Epic Games, Inc. All Rights Reserved.

#include "CompositeLayerShadowReflectionCustomization.h"

#include "ActorTreeItem.h"
#include "CompositeMeshActor.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "IDetailGroup.h"
#include "IPropertyUtilities.h"
#include "SceneOutlinerFilters.h"
#include "Layers/CompositeLayerShadowReflection.h"
#include "UI/SCompositeActorPickerTable.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "FCompositeLayerShadowReflectionCustomization"

TSharedRef<IDetailCustomization> FCompositeLayerShadowReflectionCustomization::MakeInstance()
{
	return MakeShared<FCompositeLayerShadowReflectionCustomization>();
}

void FCompositeLayerShadowReflectionCustomization::CustomizeDetails(const TSharedPtr<IDetailLayoutBuilder>& DetailBuilder)
{
	CachedDetailBuilder = DetailBuilder;
	IDetailCustomization::CustomizeDetails(DetailBuilder);
}

void FCompositeLayerShadowReflectionCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailLayout)
{
	DetailLayout.HideProperty(GET_MEMBER_NAME_CHECKED(UCompositeLayerShadowReflection, Actors));
	
	TArray<TWeakObjectPtr<UCompositeLayerShadowReflection>> Objects = DetailLayout.GetObjectsOfTypeBeingCustomized<UCompositeLayerShadowReflection>();

	IDetailCategoryBuilder& LayerCategory = DetailLayout.EditCategoryAllowNone(NAME_None);
	
	// Add all default simple properties ahead of the custom group
	TArray<TSharedRef<IPropertyHandle>> PlateProperties;
	LayerCategory.GetDefaultProperties(PlateProperties, /* bSimpleProperties */ true, /* bAdvancedProperties */ false);
	for (const TSharedRef<IPropertyHandle>& Property : PlateProperties)
	{
		if (!Property->IsCustomized())
		{
			LayerCategory.AddProperty(Property);
		}
	}

	if (Objects.Num() == 1 && Objects[0].IsValid())
	{
		UCompositeLayerShadowReflection* ShadowReflection = Cast<UCompositeLayerShadowReflection>(Objects[0].Get());
		
		IDetailGroup& ActorListGroup = LayerCategory.AddGroup("ShadowReflectionCatcherContent", LOCTEXT("ActorListGroupName", "Shadow/Reflection Catcher Content"), false, true);
		FCompositeActorPickerListRef ActorListRef(ShadowReflection, GET_MEMBER_NAME_CHECKED(UCompositeLayerShadowReflection, Actors), &ShadowReflection->Actors);
		
		ActorListGroup.AddWidgetRow()
		.WholeRowContent()
		[
			SNew(SCompositeActorPickerTable, ActorListRef)
			.SceneOutlinerFilters_Lambda([]()
			{
				TSharedPtr<FSceneOutlinerFilters> Filters = MakeShared<FSceneOutlinerFilters>();

				Filters->AddFilterPredicate<FActorTreeItem>(FActorTreeItem::FFilterPredicate::CreateLambda([](const AActor* InActor)
				{
					return !InActor->IsA<ACompositeMeshActor>();
				}));
				
				return Filters;
			})
			.OnLayoutSizeChanged(this, &FCompositeLayerShadowReflectionCustomization::OnLayoutSizeChanged)
		];
	}
	else
	{
		// Can't display actor list if multiple layers are selected, so simply put a "Multiple Values" entry in the property list
		LayerCategory.AddCustomRow(LOCTEXT("ActorListGroupName", "Shadow/Reflection Catcher Content"))
		.NameContent()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("ActorListGroupName", "Shadow/Reflection Catcher Content"))
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

void FCompositeLayerShadowReflectionCustomization::OnLayoutSizeChanged()
{
	TSharedPtr<IDetailLayoutBuilder> PinnedDetailBuilder = CachedDetailBuilder.Pin();
	if (PinnedDetailBuilder.IsValid())
	{
		PinnedDetailBuilder->GetPropertyUtilities()->RequestRefresh();
	}
}

#undef LOCTEXT_NAMESPACE

// Copyright Epic Games, Inc.All Rights Reserved.

#include "Customizations/CaptureMetadataCustomization.h"

#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"

#include "CaptureMetadata.h"

TSharedRef<IDetailCustomization> FCaptureMetadataCustomization::MakeInstance()
{
	return MakeShared<FCaptureMetadataCustomization>();
}

void FCaptureMetadataCustomization::CustomizeDetails(IDetailLayoutBuilder& InDetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> Objects;
	InDetailBuilder.GetObjectsBeingCustomized(Objects);

	check(!Objects.IsEmpty());

	TWeakObjectPtr<UCaptureMetadata> CaptureMetadataPtr = StaticCast<UCaptureMetadata*>(Objects[0].Get());

	// We are setting the Category name for all properties to group them under a owner object name
	// 
	// When showing more then one Capture Metadata, Category Name can be used to search for the owner object
	const FString NewCategoryName = CaptureMetadataPtr->GetOwnerName();
	TArray<FName> CategoryNames;
	InDetailBuilder.GetCategoryNames(CategoryNames);

	for (const FName& Category : CategoryNames)
	{
		IDetailCategoryBuilder& CategoryBuilder = InDetailBuilder.EditCategory(Category);
		CategoryBuilder.SetDisplayName(FText::FromString(NewCategoryName));
	}

	if (!CaptureMetadataPtr->IsEditable())
	{
		TFieldIterator<FProperty> Iterator(InDetailBuilder.GetBaseClass());
		while (Iterator)
		{
			FProperty* Property = *Iterator;
			check(Property->IsA<FStrProperty>());

			TSharedRef<IPropertyHandle> PropertyHandle = InDetailBuilder.GetProperty(Property->GetFName());
			IDetailPropertyRow* PropertyRow = InDetailBuilder.EditDefaultProperty(PropertyHandle);

			PropertyRow->IsEnabled(false);

			++Iterator;
		}
	}
}
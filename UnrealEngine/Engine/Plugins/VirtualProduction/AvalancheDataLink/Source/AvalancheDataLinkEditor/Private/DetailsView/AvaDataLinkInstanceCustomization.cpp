// Copyright Epic Games, Inc. All Rights Reserved.

#include "AvaDataLinkInstanceCustomization.h"
#include "AvaDataLinkInstance.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "IDetailChildrenBuilder.h"
#include "PropertyCustomizationHelpers.h"

void FAvaDataLinkInstanceCustomization::CustomizeDetails(IDetailLayoutBuilder& InDetailBuilder)
{
	TSharedRef<IPropertyHandle> DataLinkInstanceHandle = InDetailBuilder.GetProperty(UAvaDataLinkInstance::GetDataLinkInstancePropertyName());
	DataLinkInstanceHandle->MarkHiddenByCustomization();

	TSharedRef<IPropertyHandle> OutputProcessorsHandle = InDetailBuilder.GetProperty(UAvaDataLinkInstance::GetOutputProcessorsPropertyName());
	OutputProcessorsHandle->MarkHiddenByCustomization();

	IDetailCategoryBuilder& Category = InDetailBuilder.EditCategory(DataLinkInstanceHandle->GetDefaultCategoryName(), DataLinkInstanceHandle->GetDefaultCategoryText());
	Category.AddProperty(DataLinkInstanceHandle);
	Category.AddProperty(OutputProcessorsHandle);
}

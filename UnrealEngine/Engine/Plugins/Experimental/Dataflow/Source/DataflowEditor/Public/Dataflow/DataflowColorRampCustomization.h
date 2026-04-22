// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IPropertyTypeCustomization.h"

namespace UE::Dataflow
{
	class FDataflowColorRampCustomization : public IPropertyTypeCustomization
	{
	public:

		/** Makes a new instance of this detail layout class for a specific detail view requesting it */
		DATAFLOWEDITOR_API static TSharedRef<IPropertyTypeCustomization> MakeInstance();

		/** IDetailCustomization interface */
		DATAFLOWEDITOR_API virtual void CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils) override;
		virtual void CustomizeChildren(TSharedRef<IPropertyHandle> /*StructPropertyHandle*/, IDetailChildrenBuilder& /*ChildBuilder*/, IPropertyTypeCustomizationUtils& /*CustomizationUtils*/) override {}
	};
}

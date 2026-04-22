// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IDetailCustomization.h"
#include "PropertyBagDetails.h"  
#include "Tools/MetaHumanCharacterEditorCostumeTools.h"

/** A detail customization for the costume tool property bag parameters overrides */
class FCostumeParametersOverridesDetails : public FPropertyBagInstanceDataDetails
{
public:
	FCostumeParametersOverridesDetails(const TSharedPtr<IPropertyHandle> InVariableStructProperty, const TSharedPtr<IPropertyUtilities>& InPropUtils, UMetaHumanCharacterEditorCostumeItem* InItem)
		: FPropertyBagInstanceDataDetails(InVariableStructProperty, InPropUtils, /*bInFixedLayout*/ false)
		, Item(InItem)
	{
	}

protected:
	//~ Begin of FPropertyBagInstanceDataDetails interface
	virtual void OnChildRowAdded(IDetailPropertyRow& ChildRow) override;
	//~ End of FPropertyBagInstanceDataDetails interface

	/** Makes the widget use to correctly display the highlights variation property */
	TSharedRef<SWidget> MakeHighlightsVariationWidget(const TSharedRef<IPropertyHandle> PropertyHandle) const;

	/** Handles the customization of the properties which require a color picker */
	void HandleUVColorPickerProperties(const TSharedRef<IPropertyHandle> PropertyHandle, IDetailPropertyRow& ChildRow);

	/** Tries to make the UV color picker for gathering melanin and redness properties */
	void TryMakeUVColorPicker(TNotNull<UTexture2D*> ColorPickerTexture);

	/** Gets the UV coordinates values for the property with the given name */
	FVector2f GetPropertyUV(FName PropertyName, const void* PropertyBagContainerAddress) const;

	/** Called when the UV coordinates of the property with the given name have been changed */
	void OnPropertyUVChanged(const FVector2f& InUV, bool bIsDragging, FName PropertyName, void* PropertyBagContainerAddress);

	/** The property name of the current UV property being customized */
	FName UVPropertyName = NAME_None;

	/** A map that that relates the UV properties handle to its matching property name */
	TMap<FName, TTuple<TSharedPtr<IPropertyHandle>, TSharedPtr<IPropertyHandle>>> UVPropertyNameToHandlesMap;

	/** The U property handle used for customizing UV properties */
	TSharedPtr<IPropertyHandle> UPropertyHandle;

	/** The V property handle used for customizing UV properties */
	TSharedPtr<IPropertyHandle> VPropertyHandle;

	/** The detail row for the U property */
	IDetailPropertyRow* UPropertyRow = nullptr;

	/** The detail row for the V property */
	IDetailPropertyRow* VPropertyRow = nullptr;

	/** The item which holds the reference to the property bag this customization is based on */
	TWeakObjectPtr<UMetaHumanCharacterEditorCostumeItem> Item;
};

/**
 * Detail Customization for the UMetaHumanCharacterEditorCostumeItem class, which is 
 * used for editing the costume tool properties
 */
class FMetaHumanCharacterEditorCostumeItemDetailCustomization : public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance()
	{
		return MakeShared<FMetaHumanCharacterEditorCostumeItemDetailCustomization>();
	}

	//~ Begin of IDetailCustomization interface
	virtual void CustomizeDetails(IDetailLayoutBuilder& InDetailLayout) override;
	//~ End of IDetailCustomization interface
};

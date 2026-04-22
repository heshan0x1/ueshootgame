// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IDetailCustomization.h"

class SWidget;
class IDetailPropertyRow;
class IPropertyHandle;
class SCompositePlatePassPanel;
class FMenuBuilder;

/**
 * Customization for the composite plate layer, primary for displaying a custom widget for passes and composite meshes
 */
class FCompositeLayerPlateCustomization : public IDetailCustomization
{
public:
	/** Makes a new instance of the details customization */
	static TSharedRef<IDetailCustomization> MakeInstance();

	// IDetailCustomization interface
	virtual void CustomizeDetails(const TSharedPtr<IDetailLayoutBuilder>& DetailBuilder) override;
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailLayout) override;
	// End of IDetailCustomization interface

private:
	/** Customizes the Texture property in UCompositeLayerPlate to add in the media profile source selector */
	void CustomizeTexturePropertyRow(const TSharedPtr<IPropertyHandle>& InPropertyHandle, IDetailPropertyRow& InPropertyRow);

	/** Gets whether there is an active media profile configured in the editor */
	bool HasActiveMediaProfile() const;

	/** Gets the dropdown menu widget to display when the media profile button is pressed */
	TSharedRef<SWidget> GetMediaProfileSourceSelectorMenu();

	/** Sets the Texture property handle's value to the media texture corresponding to the specified media source in the active media profile */
	void SetTextureToMediaProfileSource(int32 InMediaSourceIndex);

	/** Opens the active media profile in the media profile editor */
	void OpenMediaProfile();
	
	/** Raised by the composite mesh actor list when building the Add Actor menu */
	void OnExtendCompositeMeshAddMenu(FMenuBuilder& MenuBuilder);

	/** Creates a composite mesh actor at the appropriate position in the level and adds it to the plate's composite meshes list */
	void CreateCompositeMeshActor();
	
	/** Gets whether a composite mesh actor can be created for this plate layer */
	bool CanCreateCompositeMeshActor() const;

	/** Raised when the custom widgets' layout size may have changed */
	void OnLayoutSizeChanged();
	
private:
	TWeakPtr<IDetailLayoutBuilder> CachedDetailBuilder;
	TSharedPtr<IPropertyHandle> TexturePropertyHandle;
};

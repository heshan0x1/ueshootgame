// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IDetailCustomization.h"

/**
 * Customization for the composite shadow/reflection layer, primary for displaying a custom widget for picking actors
 */
class FCompositeLayerShadowReflectionCustomization : public IDetailCustomization
{
public:
	/** Makes a new instance of the details customization */
	static TSharedRef<IDetailCustomization> MakeInstance();

	// IDetailCustomization interface
	virtual void CustomizeDetails(const TSharedPtr<IDetailLayoutBuilder>& DetailBuilder) override;
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailLayout) override;
	// End of IDetailCustomization interface

private:
	/** Raised when the custom widgets' layout size may have changed */
	void OnLayoutSizeChanged();
	
private:
	TWeakPtr<IDetailLayoutBuilder> CachedDetailBuilder;
};

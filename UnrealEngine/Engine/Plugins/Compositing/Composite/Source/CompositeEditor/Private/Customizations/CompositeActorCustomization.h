// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IDetailCustomization.h"
#include "Input/Reply.h"

class ACompositeActor;

/**
 * Customization for the composite actor.
 */
class FCompositeActorCustomization : public IDetailCustomization
{
public:
	/** Makes a new instance of the details customization */
	static TSharedRef<IDetailCustomization> MakeInstance();

	// IDetailCustomization interface
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailLayout) override;
	// End of IDetailCustomization interface

private:
	/** Raised when the 'Open Composure' button is clicked, and opens the Composure panel  */
	FReply OpenComposurePanel();

private:
	/** List of composite actors being displayed by this customization */
	TArray<TWeakObjectPtr<ACompositeActor>> CompositeActors;
};

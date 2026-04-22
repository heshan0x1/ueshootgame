// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IDetailCustomization.h"

class FReply;
class IDetailLayoutBuilder;
class UCustomizableObjectNode;


/** Base of all UCustomizableObjectNode details. */
class FCustomizableObjectNodeDetails : public IDetailCustomization
{
public:
	/** Makes a new instance of this detail layout class for a specific detail view requesting it */
	static TSharedRef<IDetailCustomization> MakeInstance();
	
	// IDetailCustomization interface
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;
	virtual void CustomizeDetails(const TSharedPtr<IDetailLayoutBuilder>& DetailBuilder) override;

	//
	FReply AddNewPin(UCustomizableObjectNode* Node);

private: 

	// Pointer to the DetailBuilder. Needed to refresh the details when we create a new pin from the pin viewer.
	TSharedPtr<IDetailLayoutBuilder> DetailBuilderPtr;
};

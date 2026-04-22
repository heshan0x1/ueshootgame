// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IDetailCustomization.h"

class IDetailLayoutBuilder;

/**
* Customizes a LiveLinkBroadcastComponent details.
*/
class FLiveLinkBroadcastComponentDetailCustomization : public IDetailCustomization
{
public:

	static TSharedRef<IDetailCustomization> MakeInstance()
	{
		return MakeShared<FLiveLinkBroadcastComponentDetailCustomization>();
	}

	// IDetailCustomization interface
	virtual void CustomizeDetails(class IDetailLayoutBuilder& DetailBuilder) override;
	// End IDetailCustomization interface

protected:
	/** Keep a reference to force refresh the layout */
	IDetailLayoutBuilder* DetailLayout = nullptr;
};

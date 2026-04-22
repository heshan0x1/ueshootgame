// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EdGraphUtilities.h"

#define UE_API UAFEDITOR_API

class FAnimNextGraphPanelPinFactoryEditor : public FGraphPanelPinFactory
{
public:
	// FGraphPanelPinFactory interface
	UE_API virtual TSharedPtr<SGraphPin> CreatePin(UEdGraphPin* InPin) const override;
};

#undef UE_API

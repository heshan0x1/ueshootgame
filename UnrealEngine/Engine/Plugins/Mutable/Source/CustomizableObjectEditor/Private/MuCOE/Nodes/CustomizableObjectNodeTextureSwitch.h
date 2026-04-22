// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuCOE/Nodes/CustomizableObjectNodeSwitchBase.h"

#include "CustomizableObjectNodeTextureSwitch.generated.h"

#define UE_API CUSTOMIZABLEOBJECTEDITOR_API


UCLASS(MinimalAPI)
class UCustomizableObjectNodeTextureSwitch : public UCustomizableObjectNodeSwitchBase
{
public:
	GENERATED_BODY()
	// UCustomizableObjectNode interface
	UE_API virtual void BackwardsCompatibleFixup(int32 CustomizableObjectCustomVersion) override;
	
	// UCustomizableObjectNodeSwitchBase interface
	UE_API virtual FName GetCategory() const override;
};

#undef UE_API

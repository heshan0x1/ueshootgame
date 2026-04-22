// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuCOE/Nodes/CustomizableObjectNodeSwitchBase.h"
#include "CONodeMaterialSwitch.generated.h"

#define UE_API CUSTOMIZABLEOBJECTEDITOR_API


UCLASS(MinimalAPI)
class UCONodeMaterialSwitch : public UCustomizableObjectNodeSwitchBase
{
public:
	GENERATED_BODY()

	// UCustomizableObjectNodeSwitchBase interface
	UE_API virtual FName GetCategory() const override;
};

#undef UE_API

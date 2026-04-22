// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuCOE/Nodes/CustomizableObjectNodeTextureVariation.h"

#include "CustomizableObjectNodePassThroughTextureVariation.generated.h"

#define UE_API CUSTOMIZABLEOBJECTEDITOR_API


UCLASS(MinimalAPI)
class UCustomizableObjectNodePassThroughTextureVariation : public UCustomizableObjectNodeTextureVariation
{
public:
	GENERATED_BODY()

	// EdGraphNode interface
	UE_API virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;

	// UCustomizableObjectNode interface
	UE_API virtual void BackwardsCompatibleFixup(int32 CustomizableObjectCustomVersion) override;
	
	// UCustomizableObjectNodeVariation interface
	UE_API virtual FName GetCategory() const override;
};

#undef UE_API

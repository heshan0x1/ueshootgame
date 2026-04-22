// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuCOE/Nodes/CustomizableObjectNodeTexture.h"

#include "CustomizableObjectNodePassThroughTexture.generated.h"

#define UE_API CUSTOMIZABLEOBJECTEDITOR_API


UCLASS(MinimalAPI, hidecategories = ("Texture2D"))
class UCustomizableObjectNodePassThroughTexture : public UCustomizableObjectNodeTextureBase
{
public:
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = Texture, Meta = (DisplayName = Texture))
	TObjectPtr<UTexture> PassThroughTexture = nullptr;

	// UCustomizableObjectNode interface
	UE_API virtual void BackwardsCompatibleFixup(int32 CustomizableObjectCustomVersion) override;
	UE_API virtual void AllocateDefaultPins(UCustomizableObjectNodeRemapPins* RemapPins) override;

	// UCustomizableObjectNodeTextureBase interface
	UE_API virtual TObjectPtr<UTexture> GetTexture() override;

	// Begin EdGraphNode interface
	UE_API virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	UE_API virtual FLinearColor GetNodeTitleColor() const override;
	UE_API virtual FText GetTooltipText() const override;

private:

	// For backwards compatibility
	UPROPERTY()
	TObjectPtr<UTexture2D> Texture_DEPRECATED = nullptr;

};

#undef UE_API

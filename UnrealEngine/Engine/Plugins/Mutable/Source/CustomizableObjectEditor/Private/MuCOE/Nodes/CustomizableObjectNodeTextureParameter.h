// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuCOE/Nodes/CustomizableObjectNodeParameter.h"

#include "CustomizableObjectNodeTextureParameter.generated.h"

#define UE_API CUSTOMIZABLEOBJECTEDITOR_API

namespace ENodeTitleType { enum Type : int; }

class UCustomizableObjectNodeRemapPins;
class UObject;
class UTexture2D;


UCLASS(MinimalAPI)
class UCustomizableObjectNodeTextureParameter : public UCustomizableObjectNodeParameter
{
public:
	GENERATED_BODY()

	/** Default value of the parameter. */
	UPROPERTY(EditAnywhere, Category=CustomizableObject)
	TObjectPtr<UTexture2D> DefaultValue;

	/** Reference Texture where this parameter copies some properties from. */
	UPROPERTY(EditAnywhere, Category=CustomizableObject)
	TObjectPtr<UTexture2D> ReferenceValue;

	/** Set the width of the Texture when there is no texture reference.*/
	UPROPERTY(EditAnywhere, Category = CustomizableObject)
	int32 TextureSizeX = 0;

	/** Set the height of the Texture when there is no texture reference.*/
	UPROPERTY(EditAnywhere, Category = CustomizableObject)
	int32 TextureSizeY = 0;

	// UCustomizableObjectNode interface
	UE_API virtual void BackwardsCompatibleFixup(int32 CustomizableObjectCustomVersion) override;
	UE_API virtual bool IsExperimental() const override;

	// CustomizableObjectNodeParameter interface
	UE_API virtual FName GetCategory() const override;
};

#undef UE_API

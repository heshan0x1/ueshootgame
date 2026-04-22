// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuCOE/Nodes/CustomizableObjectNodeParameter.h"
#include "MuCOE/Nodes/CustomizableObjectNodeMaterialInterface.h"

#include "CustomizableObjectNodeMaterialParameter.generated.h"

#define UE_API CUSTOMIZABLEOBJECTEDITOR_API

namespace ENodeTitleType { enum Type : int; }

class UMaterialInterface;


UCLASS(MinimalAPI)
class UCustomizableObjectNodeMaterialParameter : public UCustomizableObjectNodeParameter, public ICustomizableObjectNodeMaterialInterface
{
public:
	GENERATED_BODY()

	/** Default value of the parameter. */
	UPROPERTY(EditAnywhere, Category=CustomizableObject)
	TSoftObjectPtr<UMaterialInterface> DefaultValue;

	UPROPERTY(EditAnywhere, Category=CustomizableObject)
	TSoftObjectPtr<UMaterialInterface> ReferenceValue;
	
	// UCustomizableObjectNode interface
	UE_API virtual bool IsExperimental() const override;

	// CustomizableObjectNodeParameter interface
	UE_API virtual FName GetCategory() const override;

	// ICustomizableObjectNodeMaterialInterface interface
	UE_API virtual TSoftObjectPtr<UMaterialInterface> GetMaterial() const override;
	UE_API virtual UEdGraphPin* GetMaterialPin() const override;
};

#undef UE_API

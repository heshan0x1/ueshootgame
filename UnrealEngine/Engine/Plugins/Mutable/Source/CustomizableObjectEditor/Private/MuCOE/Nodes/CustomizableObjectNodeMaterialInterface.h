// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuCOE/Nodes/CustomizableObjectNode.h"

#include "CustomizableObjectNodeMaterialInterface.generated.h"

class UEdGraphPin;
class UMaterialInterface;

UINTERFACE(MinimalAPI)
class UCustomizableObjectNodeMaterialInterface : public UInterface
{
	GENERATED_BODY()
};


class ICustomizableObjectNodeMaterialInterface
{
	GENERATED_BODY()

public:
	// Own interface

	/** Returns the Unreal mesh (e.g., USkeletalMesh, UStaticMesh...). */
	virtual TSoftObjectPtr<UMaterialInterface> GetMaterial() const PURE_VIRTUAL(UCustomizableObjectNodeMaterialInterface::GetMaterial, return {};);

	/** Returns the output Mesh pin associated to the given LODIndex and SectionIndex. Override. */
	virtual UEdGraphPin* GetMaterialPin() const PURE_VIRTUAL(UCustomizableObjectNodeMaterialInterface::GetMaterialPin, return {};);

};

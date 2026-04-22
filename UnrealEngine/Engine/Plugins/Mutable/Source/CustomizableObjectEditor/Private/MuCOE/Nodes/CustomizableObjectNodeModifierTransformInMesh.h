// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuCOE/Nodes/CustomizableObjectNodeModifierBase.h"
#include "MuT/NodeModifier.h"

#include "CustomizableObjectNodeModifierTransformInMesh.generated.h"

#define UE_API CUSTOMIZABLEOBJECTEDITOR_API

namespace ENodeTitleType { enum Type : int; }

class UCustomizableObject;
class UCustomizableObjectNodeRemapPins;
class UEdGraphPin;

DECLARE_MULTICAST_DELEGATE_OneParam(OnTransformChanged, const FTransform&);

UCLASS(MinimalAPI)
class UCustomizableObjectNodeModifierTransformInMesh : public UCustomizableObjectNodeModifierBase
{
	GENERATED_BODY()

public:
	/** Transform to apply to the bounding mesh before selecting for vertices to transform. */
	UPROPERTY(EditAnywhere, Category = BoundingMesh)
	FTransform BoundingMeshTransform = FTransform::Identity;

	/**
	 * Delegate invoked each time the BoundingMeshTransform value gets modified
	 */
	OnTransformChanged TransformChangedDelegate;
	
public:
	// EdGraphNode interface
	UE_API virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	UE_API virtual FText GetTooltipText() const override;

	// UCustomizableObjectNode interface
	UE_API virtual void AllocateDefaultPins(UCustomizableObjectNodeRemapPins* RemapPins) override;
	virtual bool IsExperimental() const override { return true; }
	UE_API virtual void PostEditChangeProperty( struct FPropertyChangedEvent& PropertyChangedEvent) override;

	// UCustomizableObjectNodeModifierBase interface
	UE_API virtual UEdGraphPin* GetOutputPin() const override;

	// Own interface
	UE_API UEdGraphPin* GetBoundingMeshPin() const;
	UE_API UEdGraphPin* GetTransformPin() const;

private:
	static UE_API const TCHAR* OutputPinName;
	static UE_API const TCHAR* BoundingMeshPinName;
	static UE_API const TCHAR* TransformPinName;
	
};

#undef UE_API

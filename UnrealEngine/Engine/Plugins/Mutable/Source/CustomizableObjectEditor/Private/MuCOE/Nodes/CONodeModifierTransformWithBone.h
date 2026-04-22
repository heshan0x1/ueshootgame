// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuCOE/Nodes/CustomizableObjectNodeModifierBase.h"
#include "MuT/NodeModifier.h"

#include "CONodeModifierTransformWithBone.generated.h"

#define UE_API CUSTOMIZABLEOBJECTEDITOR_API

namespace ENodeTitleType { enum Type : int; }

class UCustomizableObject;
class UCustomizableObjectNodeRemapPins;
class UEdGraphPin;

DECLARE_MULTICAST_DELEGATE_OneParam(OnTransformChanged, const FTransform&);

UCLASS(MinimalAPI)
class UCONodeModifierTransformWithBone : public UCustomizableObjectNodeModifierBase
{
	GENERATED_BODY()

public:
	/** Transform to apply to the vertices. */
	UPROPERTY(EditAnywhere, Category = Transform)
	FTransform BoundingMeshTransform = FTransform::Identity;

	/** Root bone used to filter vertices based on skinning weights. Vertices skinned to this
	  * bone or below will me transformed. */
	UPROPERTY(EditAnywhere, Category = Transform)
	FString BoneName;

	/** Vertex influences under this threshold will not be considered when filtering vertices.
	  * Percentage of total skin weight [0.0 .. 1.0]. */
	UPROPERTY(EditAnywhere, Category = Transform, meta = (ClampMin = 0.0f, ClampMax = 1.0f))
	float ThresholdFactor = 0.05f;

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
	UE_API UEdGraphPin* GetTransformPin() const;
	
private:

	UPROPERTY()
	FEdGraphPinReference TransformPin;

	UPROPERTY()
	FEdGraphPinReference OutputPin;
};

#undef UE_API

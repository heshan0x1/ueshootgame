// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuCOE/Nodes/CustomizableObjectNode.h"

#include "CustomizableObjectNodeColorFromFloats.generated.h"

#define UE_API CUSTOMIZABLEOBJECTEDITOR_API

namespace ENodeTitleType { enum Type : int; }

class UCustomizableObjectNodeRemapPins;
class UEdGraphPin;
class UObject;
struct FPropertyChangedEvent;


UCLASS(MinimalAPI)
class UCustomizableObjectNodeColorFromFloats : public UCustomizableObjectNode
{
public:
	GENERATED_BODY()

	UE_API UCustomizableObjectNodeColorFromFloats();

	// UObject interface.
	UE_API void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;

	// Begin EdGraphNode interface
	UE_API FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	UE_API FLinearColor GetNodeTitleColor() const override;
	UE_API FText GetTooltipText() const override;

	// UCustomizableObjectNode interface
	UE_API void AllocateDefaultPins(UCustomizableObjectNodeRemapPins* RemapPins) override;

	UEdGraphPin* RPin() const
	{
		return FindPin(TEXT("R"));
	}

	UEdGraphPin* GPin() const
	{
		return FindPin(TEXT("G"));
	}

	UEdGraphPin* BPin() const
	{
		return FindPin(TEXT("B"));
	}

	UEdGraphPin* APin() const
	{
		return FindPin(TEXT("A"));
	}

	// UCustomizableObjectNode interface
	virtual bool IsAffectedByLOD() const override { return false; }
};

#undef UE_API

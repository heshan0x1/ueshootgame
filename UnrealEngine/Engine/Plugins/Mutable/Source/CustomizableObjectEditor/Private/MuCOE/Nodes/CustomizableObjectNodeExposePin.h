// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuCOE/EdGraphSchema_CustomizableObject.h"
#include "MuCOE/Nodes/CustomizableObjectNode.h"

#include "CustomizableObjectNodeExposePin.generated.h"

#define UE_API CUSTOMIZABLEOBJECTEDITOR_API

namespace ENodeTitleType { enum Type : int; }

class UCustomizableObjectNodeRemapPins;
class UEdGraphPin;
class UObject;
struct FPropertyChangedEvent;


DECLARE_MULTICAST_DELEGATE(FOnNameChangedDelegate);


/** Export Node. */
UCLASS(MinimalAPI)
class UCustomizableObjectNodeExposePin : public UCustomizableObjectNode
{
public:
	GENERATED_BODY()

	// UObject interface
	UE_API virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;

	// EdGraphNode interface
	UE_API virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	UE_API virtual FLinearColor GetNodeTitleColor() const override;
	UE_API virtual FText GetTooltipText() const override;
	virtual bool GetCanRenameNode() const override { return true; }
	UE_API virtual void OnRenameNode(const FString& NewName) override;
	UE_API virtual void BackwardsCompatibleFixup(int32 CustomizableObjectCustomVersion) override;

	// UCustomizableObjectNode interface
	UE_API virtual bool CanConnect(const UEdGraphPin* InOwnedInputPin, const UEdGraphPin* InOutputPin, bool& bOutIsOtherNodeBlocklisted, bool& bOutArePinsCompatible) const override;
	UE_API virtual void AllocateDefaultPins(UCustomizableObjectNodeRemapPins* RemapPins) override;
	UE_API virtual bool IsNodeSupportedInMacros() const override;

	/** Return the Expose Pin Node expose pin name. */
	UE_API FString GetNodeName() const;
	
	// This is actually PinCategory
	UPROPERTY()
	FName PinType;
	
	UEdGraphPin* InputPin() const
	{
		const FName InputPinName = UEdGraphSchema_CustomizableObject::GetPinCategoryName(PinType);
		return FindPin(InputPinName);
	}

	/** Will be broadcast when the UPROPERTY Name changes. */
	FOnNameChangedDelegate OnNameChangedDelegate;
	
private:
	UPROPERTY(Category = CustomizableObject, EditAnywhere)
	FString Name = "Default Name";
};

#undef UE_API

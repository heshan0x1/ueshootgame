// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuCOE/Nodes/CustomizableObjectNode.h"
#include "MuCOE/RemapPins/CustomizableObjectNodeRemapPinsByName.h"

#include "CustomizableObjectNodeExternalPin.generated.h"

#define UE_API CUSTOMIZABLEOBJECTEDITOR_API

namespace ENodeTitleType { enum Type : int; }

class FArchive;
class UCustomizableObject;
class UCustomizableObjectNodeExposePin;
class UCustomizableObjectNodeRemapPins;
class UEdGraphPin;
class UObject;


/** Import Node. */
UCLASS(MinimalAPI)
class UCustomizableObjectNodeExternalPin : public UCustomizableObjectNode
{
public:
	GENERATED_BODY()

	// UObject interface.
	UE_API virtual void Serialize(FArchive& Ar) override;
	
	// EdGraphNode interface 
	UE_API virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	UE_API virtual FLinearColor GetNodeTitleColor() const override;
	UE_API virtual FText GetTooltipText() const override;
	
	// UCustomizableObjectNode interface
	UE_API virtual void BackwardsCompatibleFixup(int32 CustomizableObjectCustomVersion) override;
	UE_API virtual void PostBackwardsCompatibleFixup() override;
	UE_API virtual void AllocateDefaultPins(UCustomizableObjectNodeRemapPins* RemapPins) override;
	UE_API virtual UCustomizableObjectNodeRemapPins* CreateRemapPinsDefault() const override;
	UE_API virtual void BeginPostDuplicate(bool bDuplicateForPIE) override;
	UE_API virtual bool CanConnect(const UEdGraphPin* InOwnedInputPin, const UEdGraphPin* InOutputPin, bool& bOutIsOtherNodeBlocklisted, bool& bOutArePinsCompatible) const override;
	UE_API virtual void ReconstructNode(UCustomizableObjectNodeRemapPins* RemapPinsMode) override;
	UE_API virtual void UpdateReferencedNodeId(const FGuid& NewGuid) override;
	UE_API virtual void PreEditChange(FProperty* PropertyAboutToChange) override;
	UE_API virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	UE_API virtual bool IsNodeSupportedInMacros() const override;
	
	// Own interface
	
	/** Set the linked Node Expose Pin node guid. */
	UE_API void SetExternalObjectNodeId(FGuid Guid);

	/** Return the external pin. Can return nullptr. */
	UE_API UEdGraphPin* GetExternalPin() const;

	/** Return the linked Expose Pin node. Return nullptr if not set. */
	UE_API UCustomizableObjectNodeExposePin* GetNodeExposePin() const;

private:
	UE_API void PrePropagateConnectionChanged();
	UE_API void PropagateConnectionChanged();
	
public:
	// This is actually PinCategory
	UPROPERTY()
	FName PinType;

	/** External Customizable Object which the linked Node Expose Pin belong to. */
	UPROPERTY()
	TObjectPtr<UCustomizableObject> ExternalObject;

private:
	
	/** Linked Node Expose Pin node guid. */
	UPROPERTY()
	FGuid ExternalObjectNodeId;
	
	FDelegateHandle OnNameChangedDelegateHandle;
	FDelegateHandle DestroyNodeDelegateHandle;

	/** Connected pins (pins connected to the Export Node pin) before changing the import/export implicit connection. */
	TArray<UEdGraphPin*> PropagatePreviousPin;
};

#undef UE_API

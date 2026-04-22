// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuCOE/Nodes/CustomizableObjectNode.h"

#include "CustomizableObjectNodeMeshMorphStackDefinition.generated.h"

#define UE_API CUSTOMIZABLEOBJECTEDITOR_API

class UCustomizableObjectNodeRemapPins;
class UEdGraphPin;
class UObject;


UCLASS(MinimalAPI)
class UCustomizableObjectNodeMeshMorphStackDefinition : public UCustomizableObjectNode
{
public:
	GENERATED_BODY()

	// EdGraphNode interface
	UE_API virtual FText GetNodeTitle(ENodeTitleType::Type TittleType)const override;
	UE_API virtual FLinearColor GetNodeTitleColor() const override;
	UE_API virtual FText GetTooltipText() const override;
	UE_API virtual void PinConnectionListChanged(UEdGraphPin* Pin) override;
	UE_API virtual bool IsNodeOutDatedAndNeedsRefresh() override;

	// UCustomizableObjectNode interface
	UE_API virtual void AllocateDefaultPins(UCustomizableObjectNodeRemapPins* RemapPins) override;
	UE_API virtual void ReconstructNode(UCustomizableObjectNodeRemapPins* RemapPins) override;

	// Own interface
	/** Returns the mesh pin of the node. */
	UE_API UEdGraphPin* GetMeshPin() const;

	/** Returns the stack pin of the node. */
	UE_API UEdGraphPin* GetStackPin() const;

	/** Returns the morph pin at Index. */
	UE_API UEdGraphPin* GetMorphPin(int32 Index) const;
	
	/** Returns the index of the next connected stack node. Returns -1 if there is none. */
	UE_API int32 NextConnectedPin(int32 Index, TArray<FString> AvailableMorphs)const;

private:
	/** Fills the list with all the morphs. */
	UE_API void UpdateMorphList();

	/** List with all the morphs of the linked skeletal mesh. */
	UPROPERTY()
	TArray<FString> MorphNames;
};

#undef UE_API

// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuCOE/Nodes/CustomizableObjectNode.h"

#include "CustomizableObjectNodeSwitchBase.generated.h"

#define UE_API CUSTOMIZABLEOBJECTEDITOR_API

namespace ENodeTitleType { enum Type : int; }

class FArchive;
class UCustomizableObjectNodeRemapPins;
class UObject;
struct FFrame;


UCLASS(MinimalAPI, Abstract)
class UCustomizableObjectNodeSwitchBase : public UCustomizableObjectNode
{
public:
	GENERATED_BODY()

	// UObject interface
	UE_API virtual void Serialize(FArchive& Ar) override;
	
	// Begin EdGraphNode interface
	UE_API virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	UE_API virtual FLinearColor GetNodeTitleColor() const override;
	UE_API virtual FText GetTooltipText() const override;
	UE_API virtual void PinConnectionListChanged(UEdGraphPin* Pin) override;
	UE_API virtual void PostPasteNode() override;

	// UCustomizableObjectNode interface
	UE_API virtual void BackwardsCompatibleFixup(int32 CustomizableObjectCustomVersion) override;
	UE_API virtual void PostBackwardsCompatibleFixup() override;
	UE_API virtual void AllocateDefaultPins(UCustomizableObjectNodeRemapPins* RemapPins) override;
	UE_API virtual void ReconstructNode(UCustomizableObjectNodeRemapPins* RemapPins) override;

	/** Get the output pin category. Override. */
	virtual FName GetCategory() const PURE_VIRTUAL(UCustomizableObjectNodeSwitchBase::GetCategory, return {}; );

	UE_API UEdGraphPin* OutputPin() const;

	UE_API UEdGraphPin* SwitchParameter() const;

	UEdGraphPin* GetElementPin(int32 Index) const
	{
		return FindPin(GetPinPrefix(Index));
	}

	UE_API int32 GetNumElements() const;

	/** Links the PostEditChangeProperty delegate */
	UE_API void LinkPostEditChangePropertyDelegate(const UEdGraphPin& Pin);

	/** Get the ouput pin name. Override. */
	UE_API virtual FString GetOutputPinName() const;

private:
	/** Get the pin prefix. Used for retrocompatibility. Override. */
	UE_API virtual FString GetPinPrefix() const;

protected:
	UPROPERTY()
	FEdGraphPinReference OutputPinReference;

	UPROPERTY()
	TArray<FString> ReloadingElementsNames;

private:
	/** Get the pin prefix with index. Used for retrocompatibility.*/
	UE_API FString GetPinPrefix(int32 Index) const;

	UE_API void ReloadEnumParam();

	/** Last NodeEnumParameter connected. Used to remove the callback once disconnected. */
	TWeakObjectPtr<UCustomizableObjectNode> LastNodeEnumParameterConnected;

	/** NodeEnumParameter property changed callback function. Reconstructs the node. */
	UFUNCTION()
	UE_API void EnumParameterPostEditChangeProperty(FPostEditChangePropertyDelegateParameters& Parameters);

	/** The node has to be reconstructed. PinConnectionListChanged(...) can not reconstruct the node, flag used to reconstruct the node on NodeConnectionListChanged(). */
	bool bMarkReconstruct = false;


	UPROPERTY()
	FEdGraphPinReference SwitchParameterPinReference;
};

#undef UE_API

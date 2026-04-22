// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuCOE/Nodes/CustomizableObjectNodeComponentMeshBase.h"

#include "CustomizableObjectNodeComponentMeshAddTo.generated.h"

#define UE_API CUSTOMIZABLEOBJECTEDITOR_API

class UCustomizableObjectNodeMacroInstance;

UCLASS(MinimalAPI)
class UCustomizableObjectNodeComponentMeshAddTo : public UCustomizableObjectNode, public ICustomizableObjectNodeComponentMeshInterface
{
	GENERATED_BODY()

public:
	// UEdGraphNode interface
	UE_API virtual void AllocateDefaultPins(UCustomizableObjectNodeRemapPins* RemapPins) override;
	UE_API virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	UE_API virtual FLinearColor GetNodeTitleColor() const override;
	UE_API virtual void PinConnectionListChanged(UEdGraphPin* Pin) override;

	// UObject interface
	UE_API virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;

	//UCustomizableObjectNode interface
	UE_API virtual bool IsAffectedByLOD() const override;
	UE_API virtual bool IsSingleOutputNode() const override;
	UE_API virtual void BackwardsCompatibleFixup(int32 CustomizableObjectCustomVersion) override;
	UE_API virtual bool CanRenamePin(const UEdGraphPin& Pin) const override;
	UE_API virtual FText GetPinEditableName(const UEdGraphPin& Pin) const override;
	UE_API virtual void SetPinEditableName(const UEdGraphPin& Pin, const FText& Value) override;
	
	// ComponentMesh Interface
	UE_API virtual int32 GetNumLODs() override;
	UE_API virtual ECustomizableObjectAutomaticLODStrategy GetAutoLODStrategy() override;
	UE_API virtual const TArray<FEdGraphPinReference>& GetLODPins() const override;
	UE_API virtual UEdGraphPin* GetOutputPin() const override;
	UE_API virtual void SetOutputPin(const UEdGraphPin* Pin) override;
	UE_API virtual const UCustomizableObjectNode* GetOwningNode() const override;

	// Own interface
	UE_API FName GetParentComponentName(TArray<const UCustomizableObjectNodeMacroInstance*>* MacroContext = nullptr) const;
	UE_API void SetParentComponentName(const FName& InComponentName);
	UE_API UEdGraphPin* GetParentComponentNamePin() const;

private:
	UPROPERTY()
	FName ParentComponentName;

public:
	UPROPERTY(EditAnywhere, Category = ComponentMesh)
	int32 NumLODs = 1;

	UPROPERTY(EditAnywhere, Category = ComponentMesh, DisplayName = "Auto LOD Strategy")
	ECustomizableObjectAutomaticLODStrategy AutoLODStrategy = ECustomizableObjectAutomaticLODStrategy::AutomaticFromMesh;

	UPROPERTY()
	TArray<FEdGraphPinReference> LODPins;

	UPROPERTY()
	FEdGraphPinReference OutputPin;

private:
	UPROPERTY()
	FEdGraphPinReference ParentComponentNamePin;
};

#undef UE_API

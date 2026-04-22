// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuCOE/Nodes/CustomizableObjectNodeMaterial.h"

#include "CustomizableObjectNodeCopyMaterial.generated.h"

#define UE_API CUSTOMIZABLEOBJECTEDITOR_API

class UCustomizableObjectNodeRemapPins;
class UCustomizableObjectNodeSkeletalMesh;
class UEdGraphPin;
class UObject;


/** Generates a new Surface with the same connections as the Parent Material Surface, but with a different Mesh. */
UCLASS(MinimalAPI)
class UCustomizableObjectNodeCopyMaterial : public UCustomizableObjectNodeMaterialBase
{
public:
	GENERATED_BODY()

	// UEdGraphNode interface
	UE_API virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	UE_API virtual FText GetTooltipText() const override;

	// UCustomizableObjectNode interface
	UE_API virtual void AllocateDefaultPins(UCustomizableObjectNodeRemapPins* RemapPins) override;
	UE_API virtual bool CanConnect(const UEdGraphPin* InOwnedInputPin, const UEdGraphPin* InOutputPin, bool& bOutIsOtherNodeBlocklisted, bool& bOutArePinsCompatible) const override;
	UE_API virtual bool ShouldBreakExistingConnections(const UEdGraphPin* InputPin, const UEdGraphPin* OutputPin) const override;
	UE_API virtual bool IsNodeOutDatedAndNeedsRefresh() override;
	UE_API virtual bool ProvidesCustomPinRelevancyTest() const override;
	UE_API virtual bool IsPinRelevant(const UEdGraphPin* Pin) const override;
	UE_API virtual void BackwardsCompatibleFixup(int32 CustomizableObjectCustomVersion) override;
	UE_API virtual TArray<FString> GetEnableTags(TArray<const UCustomizableObjectNodeMacroInstance*>* MacroContext = nullptr) override;
	UE_API virtual TArray<FString>* GetEnableTagsArray() override;

	// UCustomizableObjectNodeMaterialBase interface
	UE_API virtual UMaterialInterface* GetMaterial() const override;
	UE_API virtual UEdGraphPin* GetMaterialAssetPin() const override;
	UE_API virtual int32 GetNumParameters(EMaterialParameterType Type) const override;
	UE_API virtual FNodeMaterialParameterId GetParameterId(EMaterialParameterType Type, int32 ParameterIndex) const override;
	UE_API virtual FName GetParameterName(EMaterialParameterType Type, int32 ParameterIndex) const override;
	UE_API virtual int32 GetParameterLayerIndex(EMaterialParameterType Type, int32 ParameterIndex) const override;
	UE_API virtual FText GetParameterLayerName(EMaterialParameterType Type, int32 ParameterIndex) const override;
	UE_API virtual bool HasParameter(const FNodeMaterialParameterId& ParameterId) const override;
	UE_API virtual const UEdGraphPin* GetParameterPin(EMaterialParameterType Type, int32 ParameterIndex) const override;
	UE_API virtual UEdGraphPin* GetParameterPin(const FNodeMaterialParameterId& ParameterId) const override;
	UE_API virtual bool IsImageMutableMode(int32 ImageIndex) const override;
	UE_API virtual bool IsImageMutableMode(const UEdGraphPin& Pin) const override;
	UE_API virtual UTexture2D* GetImageReferenceTexture(int32 ImageIndex) const override;
	UE_API virtual UTexture2D* GetImageValue(int32 ImageIndex) const override;
	UE_API virtual int32 GetImageUVLayout(int32 ImageIndex) const override;
	UE_API virtual UCustomizableObjectNodeMaterial* GetMaterialNode() const override;
	UE_API virtual UEdGraphPin* GetMeshPin() const override;
	UE_API virtual FPostImagePinModeChangedDelegate* GetPostImagePinModeChangedDelegate() override;
	UE_API virtual TArray<UCustomizableObjectLayout*> GetLayouts() const override;
	UE_API virtual UEdGraphPin* OutputPin() const override;
	UE_API virtual bool RealMaterialDataHasChanged() const override;
	UE_API virtual UEdGraphPin* GetEnableTagsPin() const override;
	
	// Own interface
	UE_API UCustomizableObjectNodeSkeletalMesh* GetMeshNode() const;
	
protected:

	UE_API UEdGraphPin* GetMeshSectionPin() const;

};

#undef UE_API

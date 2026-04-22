// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuCOE/Nodes/CustomizableObjectNode.h"
#include "MuCOE/COVariable.h"
#include "MuCOE/RemapPins/CustomizableObjectNodeRemapPinsByName.h"

#include "CONodeMaterialBreak.generated.h"

#define UE_API CUSTOMIZABLEOBJECTEDITOR_API

namespace ENodeTitleType { enum Type : int; }
class UCustomizableObjectNodeRemapPins;


UCLASS()
class UCustomizableObjectNodeBreakMaterialRemapPins: public UCustomizableObjectNodeRemapPinsByName
{
public:
	GENERATED_BODY()

	virtual bool Equal(const UCustomizableObjectNode& Node, const UEdGraphPin& OldPin, const UEdGraphPin& NewPin) const override;
};


UCLASS(MinimalAPI)
class UCONodeMaterialBreakPinData : public UCustomizableObjectNodePinData
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, Category = NoCategory)
	FCOVariable VariableData;

};


UCLASS(MinimalAPI)
class UCONodeMaterialBreak : public UCustomizableObjectNode
{
public:

	GENERATED_BODY()

	// Begin EdGraphNode interface
	UE_API virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	UE_API virtual FLinearColor GetNodeTitleColor() const override;
	UE_API virtual FText GetTooltipText() const override;

	// UCustomizableObjectNode interface
	UE_API virtual void AllocateDefaultPins(UCustomizableObjectNodeRemapPins* RemapPins) override;
	UE_API virtual TSharedPtr<IDetailsView> CustomizePinDetails(const UEdGraphPin& Pin) const override;
	UE_API virtual bool HasPinViewer() const override;
	UE_API virtual bool CanCreatePinsFromPinViewer() const override;
	UE_API virtual void CreatePinFromPinViewer() override;
	UE_API virtual UCustomizableObjectNodeRemapPins* CreateRemapPinsDefault() const override;
	UE_API virtual TArray<FName> GetAllowedPinViewerCreationTypes() const override;

	// Own interface
	FName GetPinParameterName(const UEdGraphPin& Pin) const;

public: 

	UPROPERTY()
	FEdGraphPinReference MaterialPinRef;
};

#undef UE_API

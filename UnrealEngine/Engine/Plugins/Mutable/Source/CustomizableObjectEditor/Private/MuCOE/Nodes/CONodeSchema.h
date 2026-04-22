// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuCOE/EdGraphSchema_CustomizableObject.h"
#include "MuCOE/Nodes/CustomizableObjectNode.h"

#include "CONodeSchema.generated.h"

#define LOCTEXT_NAMESPACE "CustomizableObjectEditor"


UCLASS()
class UCONodeSchemaPinData : public UCustomizableObjectNodePinData
{
	GENERATED_BODY()

public:	
	UPROPERTY()
	FLinearColor Color;

	UPROPERTY()
	bool bIsPassthrough = false;

	UPROPERTY()
	bool bIsEditable = false;
	
	UPROPERTY()
	FText Name;
};


USTRUCT()
struct FPinSchema
{
	GENERATED_BODY()
	
	UPROPERTY(EditAnywhere, Category = Schema)
	FText FriendlyName = LOCTEXT("Pin", "Pin");
	
	UPROPERTY(EditAnywhere, Category = Schema)
	bool bIsInput = false;

	UPROPERTY(EditAnywhere, Category = Schema)
	bool bIsArray = false;
	
	UPROPERTY(EditAnywhere, Category = Schema)
	bool bIsPassthrough = false;

	UPROPERTY(EditAnywhere, Category = Schema)
	bool bIsEditable = false;
	
	UPROPERTY(EditAnywhere, Category = Schema)
	FLinearColor Color = FLinearColor::White;
};


UCLASS(MinimalAPI)
class UCONodeSchema : public UCustomizableObjectNode
{
public:
	GENERATED_BODY()

	// UObject interface
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;

	// UEdGraphNode interface
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FLinearColor GetNodeTitleColor() const override;

	// UCustomizableObjectNode interface
	virtual void AllocateDefaultPins(UCustomizableObjectNodeRemapPins* RemapPins) override;
	virtual FLinearColor GetPinColor(const UEdGraphPin &Pin) const override;
	virtual bool IsPassthrough(const UEdGraphPin& Pin) const override;
	virtual bool CanRenamePin(const UEdGraphPin& Pin) const override;
	virtual FText GetPinEditableName(const UEdGraphPin& Pin) const override;
	virtual void SetPinEditableName(const UEdGraphPin& Pin, const FText& Value) override;

protected:
	virtual UCustomizableObjectNodeRemapPins* CreateRemapPinsDefault() const override;

public:
	UPROPERTY(EditAnywhere, Category = Schema)
	FText Title = LOCTEXT("Schema", "Schema");

	UPROPERTY(EditAnywhere, Category = Schema)
	FLinearColor TitleColor;
	
	UPROPERTY(EditAnywhere, Category = Schema)
	TArray<FPinSchema> PinSchemas;
};


#undef LOCTEXT_NAMESPACE


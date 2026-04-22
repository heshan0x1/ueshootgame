// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuCOE/Nodes/CustomizableObjectNodeSwitchBase.h"
#include "MuCOE/ICustomizableObjectExtensionNode.h"

#include "CustomizableObjectNodeExtensionDataSwitch.generated.h"

#define UE_API CUSTOMIZABLEOBJECTEDITOR_API

UCLASS(MinimalAPI, abstract)
class UCustomizableObjectNodeExtensionDataSwitch
	: public UCustomizableObjectNodeSwitchBase
	, public ICustomizableObjectExtensionNode
{
	GENERATED_BODY()

public:

	//~Begin UCustomizableObjectNode interface
	UE_API virtual bool IsAffectedByLOD() const override;
	//~End UCustomizableObjectNode interface

	//~Begin UCustomizableObjectNodeSwitchBase interface
	UE_API virtual bool ShouldAddToContextMenu(FText& OutCategory) const override;
	UE_API virtual FString GetPinPrefix() const override;
	//~End UCustomizableObjectNodeSwitchBase interface

	//~Begin ICustomizableObjectExtensionNode interface
	UE_API UE::Mutable::Private::Ptr<UE::Mutable::Private::NodeExtensionData> GenerateMutableNode(class FExtensionDataCompilerInterface& InCompilerInterface) const override;
	//~End ICustomizableObjectExtensionNode interface
};

#undef UE_API

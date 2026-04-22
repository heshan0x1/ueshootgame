// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RigVMModel/Nodes/RigVMTemplateNode.h"
#include "RigVMFunctionInterfaceNode.generated.h"

#define UE_API RIGVMDEVELOPER_API

/**
 * The Function Interface node is is used as the base class for
 * both the entry and return nodes.
 */
UCLASS(MinimalAPI, BlueprintType)
class URigVMFunctionInterfaceNode : public URigVMTemplateNode
{
	GENERATED_BODY()

public:

	// Override template node functions
	UE_API virtual uint32 GetStructureHash() const override;

	// Override node functions
	UE_API virtual FLinearColor GetNodeColor() const override;
	UE_API virtual bool IsDefinedAsVarying() const override;
	
	// URigVMNode interface
	UE_API virtual  FText GetToolTipText() const override;
	UE_API virtual FText GetToolTipTextForPin(const URigVMPin* InPin) const override;

	// returns true if the interface pin is used for anything within the function / collapse graph
	UE_API virtual bool IsInterfacePinUsed(const FName& InInterfacePinName) const;

	UE_API const URigVMPin* FindReferencedPin(const URigVMPin* InPin) const;
	UE_API const URigVMPin* FindReferencedPin(const FString& InPinPath) const;

private:

	friend class URigVMController;
};

#undef UE_API

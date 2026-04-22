// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AnimNextRigVMAssetSchema.h"
#include "AnimNextAnimationGraphSchema.generated.h"

UCLASS(MinimalAPI)
class UAnimNextAnimationGraphSchema : public UAnimNextRigVMAssetSchema
{
	GENERATED_BODY()

public:
	// URigVMSchema interface
	virtual bool CanSetNodeTitle(URigVMController* InController, const URigVMNode* InNode) const override;
	virtual bool CanRecolorNode(URigVMController* InController, const URigVMNode* InNode, const FLinearColor& InNewColor) const override;
	virtual bool CanUnfoldPin(URigVMController* InController, const URigVMPin* InPinToUnfold) const override;
	virtual bool ShouldUnfoldStruct(URigVMController* InController, const UStruct* InStruct) const override;
};

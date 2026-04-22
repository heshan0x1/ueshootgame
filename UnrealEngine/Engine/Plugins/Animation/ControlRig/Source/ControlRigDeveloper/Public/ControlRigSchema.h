// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RigVMModel/RigVMSchema.h"
#include "ControlRigSchema.generated.h"

#define UE_API CONTROLRIGDEVELOPER_API

UCLASS(MinimalAPI, BlueprintType)
class UControlRigSchema : public URigVMSchema
{
	GENERATED_UCLASS_BODY()

public:

	UE_API virtual bool ShouldUnfoldStruct(URigVMController* InController, const UStruct* InStruct) const override;

	UE_API virtual bool SupportsUnitFunction(URigVMController* InController, const FRigVMFunction* InUnitFunction) const override;
};

#undef UE_API

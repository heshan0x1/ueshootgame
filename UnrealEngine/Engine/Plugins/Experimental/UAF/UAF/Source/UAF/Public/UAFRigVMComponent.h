// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UAFAssetInstanceComponent.h"
#include "RigVMCore/RigVMExecuteContext.h"
#include "UAFRigVMComponent.generated.h"

struct FAnimNextGraphInstance;
struct FAnimNextModuleInstance;

#define UE_API UAF_API

// Asset instance component supplying work memory for RigVM execution
USTRUCT()
struct FUAFRigVMComponent : public FUAFAssetInstanceComponent
{
	GENERATED_BODY()

	FUAFRigVMComponent();

	// Get the RigVM extended execute context
	FRigVMExtendedExecuteContext& GetExtendedExecuteContext()
	{
		check(ExtendedExecuteContext.VMHash != 0);
		return ExtendedExecuteContext;
	}

private:
	friend FAnimNextGraphInstance;
	friend FAnimNextModuleInstance;

	// Extended execute context instance for our asset instance, we own it
	UPROPERTY(Transient)
	FRigVMExtendedExecuteContext ExtendedExecuteContext;
};

#undef UE_API
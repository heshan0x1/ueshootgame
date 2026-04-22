// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimNextRigVMAssetSchema.h"
#include "AnimNextExecuteContext.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AnimNextRigVMAssetSchema)

UAnimNextRigVMAssetSchema::UAnimNextRigVMAssetSchema()
{
	SetExecuteContextStruct(FAnimNextExecuteContext::StaticStruct());
}

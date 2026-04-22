// Copyright Epic Games, Inc. All Rights Reserved.

#include "Variables/RigUnit_CopyModuleProxyVariables.h"
#include "Module/AnimNextModuleInstance.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RigUnit_CopyModuleProxyVariables)

FRigUnit_CopyModuleProxyVariables_Execute()
{
	const FAnimNextModuleContextData& ContextData = ExecuteContext.GetContextData<FAnimNextModuleContextData>();
	ContextData.GetModuleInstance().CopyProxyVariables();
}

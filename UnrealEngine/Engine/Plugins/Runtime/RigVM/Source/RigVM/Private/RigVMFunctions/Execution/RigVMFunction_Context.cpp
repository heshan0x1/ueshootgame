// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigVMFunctions/Execution/RigVMFunction_Context.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RigVMFunction_Context)

FRigVMFunction_IsHostBeingDebugged_Execute()
{
#if WITH_EDITOR
	Result = ExecuteContext.IsHostBeingDebugged();
#else
	Result = false;
#endif
}



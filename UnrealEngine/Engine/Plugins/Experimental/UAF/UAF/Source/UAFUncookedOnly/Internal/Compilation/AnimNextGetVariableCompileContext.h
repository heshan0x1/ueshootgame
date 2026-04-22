// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Compilation/AnimNextRigVMAssetCompileContext.h"

#define UE_API UAFUNCOOKEDONLY_API

class UAnimNextRigVMAssetEditorData;

/**
 * Struct holding temporary compilation info during function header population
 */
struct FAnimNextGetVariableCompileContext
{
	UE_API FAnimNextGetVariableCompileContext(FAnimNextRigVMAssetCompileContext& InCompilerContext);

public:

	UE_API const TArray<FAnimNextProgrammaticFunctionHeader>& GetFunctionHeaders() const;

	UE_API const TArray<FAnimNextProgrammaticVariable>& GetProgrammaticVariables() const;
	UE_API TArray<FAnimNextProgrammaticVariable>& GetMutableProgrammaticVariables();
	UE_API const UAnimNextRigVMAssetEditorData* GetOwningAssetEditorData() const;
	
	UE_API const FAnimNextRigVMAssetCompileContext& GetAssetCompileContext() const;

protected:

	FAnimNextRigVMAssetCompileContext& CompilerContext;
};

#undef UE_API

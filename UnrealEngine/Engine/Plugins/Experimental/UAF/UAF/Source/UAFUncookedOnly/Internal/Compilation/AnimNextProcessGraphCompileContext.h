// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Compilation/AnimNextRigVMAssetCompileContext.h"

#define UE_API UAFUNCOOKEDONLY_API

class UAnimNextRigVMAssetEditorData;

/**
 * Struct holding temporary compilation info during function header population
 */
struct FAnimNextProcessGraphCompileContext
{
	UE_API FAnimNextProcessGraphCompileContext(FAnimNextRigVMAssetCompileContext& InCompilerContext);

public:

	UE_API const TArray<URigVMGraph*>& GetAllGraphs() const;
	UE_API TArray<URigVMGraph*>& GetMutableAllGraphs();
	UE_API const UAnimNextRigVMAssetEditorData* GetOwningAssetEditorData() const;

protected:

	FAnimNextRigVMAssetCompileContext& CompilerContext;
};

#undef UE_API

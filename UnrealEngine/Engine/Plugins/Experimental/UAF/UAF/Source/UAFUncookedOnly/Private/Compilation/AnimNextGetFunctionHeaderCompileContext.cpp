// Copyright Epic Games, Inc. All Rights Reserved.

#include "Compilation/AnimNextGetFunctionHeaderCompileContext.h"

FAnimNextGetFunctionHeaderCompileContext::FAnimNextGetFunctionHeaderCompileContext(FAnimNextRigVMAssetCompileContext& InCompilerContext)
	: CompilerContext(InCompilerContext)
{
}

const TArray<FAnimNextProgrammaticFunctionHeader>& FAnimNextGetFunctionHeaderCompileContext::GetFunctionHeaders() const
{
	return CompilerContext.FunctionHeaders;
}

void FAnimNextGetFunctionHeaderCompileContext::AddUniqueFunctionHeader(const FAnimNextProgrammaticFunctionHeader& InFunctionHeader)
{
	CompilerContext.FunctionHeaders.AddUnique(InFunctionHeader);
}

const UAnimNextRigVMAssetEditorData* FAnimNextGetFunctionHeaderCompileContext::GetOwningAssetEditorData() const
{
	return CompilerContext.OwningAssetEditorData;
}

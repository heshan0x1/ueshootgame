// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "StateTreeCompiler.h"

#define UE_API STATETREEEDITORMODULE_API

namespace UE::StateTree::Compiler
{

/**
 * 
 */
class FCompilerManager final
{
public:
	static UE_API void Startup();
	static UE_API void Shutdown();

	static UE_API bool CompileSynchronously(TNotNull<UStateTree*> StateTree);
	static UE_API bool CompileSynchronously(TNotNull<UStateTree*> StateTree, FStateTreeCompilerLog& Log);
	
private:
	FCompilerManager() = delete;
	FCompilerManager(const FCompilerManager&) = delete;
	FCompilerManager& operator= (const FCompilerManager&) = delete;
};

} // UE::StateTree::Compiler

#undef UE_API

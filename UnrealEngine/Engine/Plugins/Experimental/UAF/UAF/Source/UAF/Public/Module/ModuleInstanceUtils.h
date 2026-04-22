// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "ModuleHandle.h"

struct FAnimNextParamType;
struct FAnimNextVariableReference;
struct FAnimNextModuleInstance;
enum class EPropertyBagResult : uint8;

#define UE_API UAF_API

namespace UE::UAF
{

struct FModuleContext
{
	FModuleHandle Handle;
	TObjectPtr<UObject> ContextObject;
};

/**
 * Sets the value of the specified variable in the specified animation module
 * @param InModuleContext	The context providing the handle of the module whose variable we are setting and some additional information to access the module.
 * @param InVariable		The variable to set
 * @param InType			The type of the variable to set
 * @param InData			The data to set the variable with
 * @return Whether the variable was set successfully
 */
UE_API EPropertyBagResult SetModuleVariable(FModuleContext&& InModuleContext, const FAnimNextVariableReference& InVariable, const FAnimNextParamType& InType, TConstArrayView<uint8> InData);

/**
 * Accesses the variable of the specified name for writing.
 * General read access is not provided via this API due to the current double-buffering strategy used to communicate variable writes to worker threads.
 * This is intended to allow for copy-free writing of larger data structures & arrays, rather than read access.
 * @param InModuleContext	The context providing the handle of the module whose variable we are setting and some additional information to access the module.
 * @param InVariable		The variable we want to access
 * @param InType			The type of the variable we want to access
 * @param InFunction		Function that will be called to allow modification of the variables
 * @return Whether the variable was accessed successfully
 */
UE_API EPropertyBagResult WriteModuleVariable(FModuleContext&& InModuleContext, const FAnimNextVariableReference& InVariable, const FAnimNextParamType& InType, TFunctionRef<void(TArrayView<uint8>)> InFunction);

} // UE::UAF

#undef UE_API
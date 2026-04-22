// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Materials/MaterialIRCommon.h"
#include "MaterialShared.h"

#if WITH_EDITOR

namespace MIR::Internal {

// Returns the material value tpye of the specified UTexture or URuntimeVirtualTexture.
EMaterialValueType GetTextureMaterialValueType(const UObject* TextureObject);

//
EMaterialTextureParameterType TextureMaterialValueTypeToParameterType(EMaterialValueType Type);

// Returns the value flowing into given expression input (previously set through `BindValueToExpressionInput`).
FValue* FetchValueFromExpressionInput(FMaterialIRModuleBuilderImpl* Builder, const FExpressionInput* Input);

// Flows a value into given expression input.
void BindValueToExpressionInput(FMaterialIRModuleBuilderImpl* Builder, const FExpressionInput* Input, FValue* Value);

// Flows a value int given expression output.
void BindValueToExpressionOutput(FMaterialIRModuleBuilderImpl* Builder, const FExpressionOutput* Output, FValue* Value);

// Computes the hash of a blob of memory. Note: Ptr must be 4 bytes aligned.
uint32 HashBytes(const void* Ptr, uint32 Size);

/* Other helper functions */

template <typename TKey, typename TValue>
bool Find(const TMap<TKey, TValue>& Map, const TKey& Key, TValue& OutValue)
{
	if (auto ValuePtr = Map.Find(Key)) {
		OutValue = *ValuePtr;
		return true;
	}
	return false;
}

} // namespace MIR::Internal

#endif // #if WITH_EDITOR


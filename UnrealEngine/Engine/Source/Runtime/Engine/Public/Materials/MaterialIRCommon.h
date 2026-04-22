// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "SceneTypes.h"
#include "RHIShaderPlatform.h"
#include "Containers/Array.h"
#include "Math/MathFwd.h"
#include "Misc/MemStack.h"

#if WITH_EDITOR

/* Forward declarations */

class FMaterial;
class FMaterialIRModule;
class ITargetPlatform;
class UMaterial;
class UMaterialAggregate;
class UMaterialExpression;
class UMaterialExpressionMaterialFunctionCall;
class UTexture;
enum EMaterialProperty : int;
struct FExpressionInput;
struct FExpressionOutput;
struct FMaterialAggregateAttribute;
struct FMaterialInputDescription;
struct FMaterialInsights;
struct FMaterialIRModuleBuilder;
struct FMaterialIRModuleBuilderImpl;
struct FShaderCompilerEnvironment;
struct FStaticParameterSet;

namespace UE::Shader
{
	struct FValue;
}

namespace ERHIFeatureLevel { enum Type : int; }

namespace MIR
{

/* Types*/
struct FType;
struct FPrimitive;
struct FObjectType;
struct FAggregateType;

/* IR */
struct FValue;
struct FInstruction;
struct FSetMaterialOutput;
enum class EExternalInput;

/* Others */
class FEmitter;
struct FBlock;

namespace Internal
{
	uint32 HashBytes(const char* Ptr, uint32 Size);
}

/* Helper structures */

// Use this to efficiently allocate a temporary array using MemStack, instead
// of using TArray and going through the global allocator. Only declare local variable
// of this struct (i.e. do not "new TTemporaryArray")
// Remark: The allocated memory lifespan is the same as the TTemporaryArray local variable
// (it is deallocated when TTemporaryArray goes out of scope).
template <typename T>
struct TTemporaryArray : public TArrayView<T>
{
	FMemMark MemMark;

	TTemporaryArray(int32 Num)
	: MemMark{ FMemStack::Get() }
	{
		*static_cast<TArrayView<T>*>(this) = { (T*)FMemStack::Get().Alloc(sizeof(T) * Num, alignof(T)), Num };
	}

	operator TConstArrayView<T>() const
	{
		return { TArrayView<T>::GetData(), TArrayView<T>::Num() };
	}

	void Zero()
	{
		FMemory::Memzero(TArrayView<T>::GetData(), sizeof(T) * TArrayView<T>::Num());
	}
};

template <typename T>
void ZeroArray(TArrayView<T> Array)
{
	static_assert(TIsTriviallyCopyAssignable<T>::Value);
	FMemory::Memzero(Array.GetData(), Array.Num() * sizeof(T));
}

}

#define UE_MIR_UNREACHABLE() { check(!"Unreachable"); UE_ASSUME(false); }
#define UE_MIR_TODO() UE_MIR_UNREACHABLE()

#endif // #if WITH_EDITOR

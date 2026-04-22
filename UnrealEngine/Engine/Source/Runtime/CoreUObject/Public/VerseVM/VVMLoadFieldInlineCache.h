// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_VERSE_VM || defined(__INTELLISENSE__)

#include "VVMAccessor.h"
#include "VVMGlobalTrivialEmergentTypePtr.h"

namespace Verse
{

struct VFunction;
struct VNativeFunction;

struct FLoadFieldCacheCase
{
	FLoadFieldCacheCase() = default;
	FLoadFieldCacheCase(const FLoadFieldCacheCase& Other)
		: Kind(Other.Kind)
		, EmergentTypeOffset(Other.EmergentTypeOffset)
	{
		U.Offset = Other.U.Offset;
	}

	FLoadFieldCacheCase& operator=(const FLoadFieldCacheCase& Other)
	{
		Kind = Other.Kind;
		EmergentTypeOffset = Other.EmergentTypeOffset;
		U.Offset = Other.U.Offset;
		return *this;
	}

	enum class EKind : uint8
	{
		Offset,
		ConstantValue,
		ConstantFunction,
		ConstantNativeFunction,
		Accessor,
		Invalid
	};

	static FLoadFieldCacheCase Offset(VEmergentType* EmergentType, uint64 Offset)
	{
		FLoadFieldCacheCase Result;
		Result.Kind = EKind::Offset;
		Result.EmergentTypeOffset = FHeap::EmergentTypePtrToOffset(EmergentType);
		Result.U.Offset = Offset;
		return Result;
	}

	static FLoadFieldCacheCase Constant(VEmergentType* EmergentType, VValue Value)
	{
		FLoadFieldCacheCase Result;
		Result.Kind = EKind::ConstantValue;
		Result.EmergentTypeOffset = FHeap::EmergentTypePtrToOffset(EmergentType);
		Result.U.Value = Value;
		return Result;
	}

	static FLoadFieldCacheCase Function(VEmergentType* EmergentType, VFunction* Function)
	{
		FLoadFieldCacheCase Result;
		Result.Kind = EKind::ConstantFunction;
		Result.EmergentTypeOffset = FHeap::EmergentTypePtrToOffset(EmergentType);
		Result.U.Function = Function;
		V_DIE_UNLESS(Function);
		return Result;
	}

	static FLoadFieldCacheCase NativeFunction(VEmergentType* EmergentType, VNativeFunction* NativeFunction)
	{
		FLoadFieldCacheCase Result;
		Result.Kind = EKind::ConstantNativeFunction;
		Result.EmergentTypeOffset = FHeap::EmergentTypePtrToOffset(EmergentType);
		Result.U.NativeFunction = NativeFunction;
		V_DIE_UNLESS(NativeFunction);
		return Result;
	}

	static FLoadFieldCacheCase Accessor(VEmergentType* EmergentType, VAccessor* Accessor)
	{
		FLoadFieldCacheCase Result;
		Result.Kind = EKind::Accessor;
		Result.EmergentTypeOffset = FHeap::EmergentTypePtrToOffset(EmergentType);
		Result.U.Accessor = Accessor;
		V_DIE_UNLESS(Accessor);
		return Result;
	}

	explicit operator bool() { return Kind != EKind::Invalid; }

	EKind Kind = EKind::Invalid;
	uint32 EmergentTypeOffset = 0;
	union
	{
		uint64 Offset = 0;
		VValue Value;
		VFunction* Function;
		VNativeFunction* NativeFunction;
		VAccessor* Accessor;
	} U;
};

} // namespace Verse
#endif // WITH_VERSE_VM

// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_VERSE_VM || defined(__INTELLISENSE__)

#include "VVMGlobalTrivialEmergentTypePtr.h"
#include "VerseVM/VVMFloat.h"
#include "VerseVM/VVMType.h"

namespace Verse
{

// An float type with constraints.
struct VFloatType : VType
{
	DECLARE_DERIVED_VCPPCLASSINFO(COREUOBJECT_API, VType);
	COREUOBJECT_API static TGlobalTrivialEmergentTypePtr<&StaticCppClassInfo> GlobalTrivialEmergentType;

	static VFloatType& New(FAllocationContext Context, VFloat InMin, VFloat InMax)
	{
		return *new (Context.AllocateFastCell(sizeof(VFloatType))) VFloatType(Context, InMin, InMax);
	}
	static bool Equals(const VType& Type, VFloat Min, VFloat Max)
	{
		if (Type.IsA<VFloatType>())
		{
			const VFloatType& Other = Type.StaticCast<VFloatType>();
			return Min == Other.GetMin() && Max == Other.GetMax();
		}
		return false;
	}

	const VFloat& GetMin() const
	{
		return Min;
	}

	const VFloat& GetMax() const
	{
		return Max;
	}

	bool SubsumesImpl(FAllocationContext Context, VValue);

	COREUOBJECT_API void AppendToStringImpl(FAllocationContext Context, FUtf8StringBuilderBase& Builder, EValueStringFormat Format, TSet<const void*>& VisitedObjects, uint32 RecursionDepth);
	COREUOBJECT_API TSharedPtr<FJsonValue> ToJSONImpl(FRunningContext Context, EValueJSONFormat Format, TMap<const void*, EVisitState>& VisitedObjects, const VerseVMToJsonCallback& Callback, uint32 RecursionDepth, FJsonObject* Defs);
	COREUOBJECT_API VValue FromJSONImpl(FRunningContext Context, const FJsonValue& JsonValue, EValueJSONFormat Format);

	static constexpr bool SerializeIdentity = false;
	static void SerializeLayout(FAllocationContext Context, VFloatType*& This, FStructuredArchiveVisitor& Visitor);
	void SerializeImpl(FAllocationContext, FStructuredArchiveVisitor& Visitor);

private:
	explicit VFloatType(FAllocationContext& Context, VFloat InMin, VFloat InMax)
		: VType(Context, &GlobalTrivialEmergentType.Get(Context))
		, Min(InMin)
		, Max(InMax)
	{
	}

	VFloat Min;
	VFloat Max;
};
} // namespace Verse

#endif
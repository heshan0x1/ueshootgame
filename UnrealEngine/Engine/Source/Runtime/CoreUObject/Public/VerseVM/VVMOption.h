// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_VERSE_VM || defined(__INTELLISENSE__)

#include "CoreTypes.h"
#include "Misc/AssertionMacros.h"
#include "VVMContext.h"
#include "VVMGlobalTrivialEmergentTypePtr.h"
#include "VVMType.h"
#include "VVMValue.h"

namespace Verse
{
enum class EValueStringFormat;

// Inherits from VType because true, which is option{false}, is a type.
struct VOption : VType
{
	DECLARE_DERIVED_VCPPCLASSINFO(COREUOBJECT_API, VType);
	COREUOBJECT_API static TGlobalTrivialEmergentTypePtr<&StaticCppClassInfo> GlobalTrivialEmergentType;

	static VOption& New(FAllocationContext Context, VValue InValue)
	{
		return *new (Context.AllocateFastCell(sizeof(VOption))) VOption(Context, InValue);
	}

	VValue GetValue() const
	{
		return Value.Get().Follow();
	}

	void SetValue(FAllocationContext Context, VValue InValue)
	{
		check(!InValue.IsUninitialized());
		Value.Set(Context, InValue);
	}

	COREUOBJECT_API uint32 GetTypeHashImpl();
	void VisitMembersImpl(FAllocationContext Context, FDebuggerVisitor& Visitor);
	void AppendToStringImpl(FAllocationContext Context, FUtf8StringBuilderBase& Builder, EValueStringFormat Format, TSet<const void*>& VisitedObjects, uint32 RecursionDepth);
	COREUOBJECT_API TSharedPtr<FJsonValue> ToJSONImpl(FRunningContext Context, EValueJSONFormat Format, TMap<const void*, EVisitState>& VisitedObjects, const VerseVMToJsonCallback& Callback, uint32 RecursionDepth, FJsonObject* Defs);

	static constexpr bool SerializeIdentity = false;
	static void SerializeLayout(FAllocationContext Context, VOption*& This, FStructuredArchiveVisitor& Visitor);
	void SerializeImpl(FAllocationContext Context, FStructuredArchiveVisitor& Visitor);

private:
	VOption(FAllocationContext Context, VValue InValue)
		: VType(Context, &GlobalTrivialEmergentType.Get(Context))
		, Value(Context, InValue)
	{
	}

	TWriteBarrier<VValue> Value;
};

} // namespace Verse
#endif // WITH_VERSE_VM

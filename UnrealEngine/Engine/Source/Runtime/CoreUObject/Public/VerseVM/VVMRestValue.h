// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_VERSE_VM || defined(__INTELLISENSE__)

#include "VVMCell.h"
#include "VVMValue.h"
#include "VVMWriteBarrier.h"

namespace Verse
{
struct VFrame;

struct VRestValue
{
	VRestValue(const VRestValue&) = default;
	VRestValue& operator=(const VRestValue&) = default;

	explicit constexpr VRestValue(uint16 SplitDepth)
	{
		Reset(SplitDepth);
	}

	VRestValue(FAccessContext Context, VValue Value)
		: Value(Context, Value)
	{
	}

	constexpr void Reset(uint16 SplitDepth)
	{
		SetNonCellNorPlaceholder(VValue::Root(SplitDepth));
	}

	void ResetTransactionally(FAllocationContext Context, uint16 SplitDepth)
	{
		SetNonCellNorPlaceholderTransactionally(Context, VValue::Root(SplitDepth));
	}

	void ResetTrailed(FAllocationContext Context, uint16 SplitDepth)
	{
		SetNonCellNorPlaceholderTrailed(Context, VValue::Root(SplitDepth));
	}

	void Set(FAccessContext Context, VValue NewValue)
	{
		checkSlow(!NewValue.IsRoot());
		Value.Set(Context, NewValue);
	}

	void SetTransactionally(FAllocationContext, VValue);

	void SetTrailed(FAllocationContext, VValue);

	constexpr void SetNonCellNorPlaceholder(VValue NewValue)
	{
		Value.SetNonCellNorPlaceholder(NewValue);
	}

	void SetNonCellNorPlaceholderTransactionally(FAllocationContext Context, VValue NewValue)
	{
		Value.SetNonCellNorPlaceholderTransactionally(Context, NewValue);
	}

	void SetNonCellNorPlaceholderTrailed(FAllocationContext Context, VValue NewValue)
	{
		Value.SetNonCellNorPlaceholderTrailed(Context, NewValue);
	}

	bool IsRoot() const
	{
		return Value.Get().IsRoot();
	}

	bool CanDefQuickly() const
	{
		return IsRoot();
	}

	VValue Get(FAllocationContext Context);

	VValue GetRaw()
	{
		return Value.Get();
	}

	COREUOBJECT_API VValue GetSlow(FAllocationContext Context);

	VValue GetTransactionally(FAllocationContext Context);

	COREUOBJECT_API VValue GetSlowTransactionally(FAllocationContext Context);

	bool IsUninitialized() const
	{
		return Value.Get().IsUninitialized();
	}

	bool operator==(const VRestValue& Other) const;

	COREUOBJECT_API void AppendToString(FAllocationContext Context, FUtf8StringBuilderBase& Builder, EValueStringFormat Format, TSet<const void*>& VisitedObjects, uint32 RecursionDepth = 0) const;
	COREUOBJECT_API FUtf8String ToString(FAllocationContext Context, EValueStringFormat Format, uint32 RecursionDepth = 0) const;
	COREUOBJECT_API TSharedPtr<FJsonValue> ToJSON(FRunningContext Context, EValueJSONFormat Format, TMap<const void*, EVisitState>& VisitedObjects, const VerseVMToJsonCallback& Callback, uint32 RecursionDepth = 0, FJsonObject* Defs = nullptr) const;

	template <typename TVisitor>
	friend void Visit(TVisitor& Visitor, VRestValue& InValue)
	{
		Visitor.Visit(InValue.Value, TEXT(""));
	}

	friend uint32 GetTypeHash(VRestValue RestValue);

private:
	VRestValue() = default;
	TWriteBarrier<VValue> Value;

	friend struct VArray;
	friend struct VFrame;
	friend struct VObject;
	friend struct VValue;
	friend struct VValueObject;
	friend struct FStructuredArchiveVisitor;
	friend ::FReferenceCollector;
};
} // namespace Verse

inline void FReferenceCollector::AddReferencedVerseValue(Verse::VRestValue& InValue, const UObject* ReferencingObject, const FProperty* ReferencingProperty)
{
	AddReferencedVerseValue(InValue.Value, ReferencingObject, ReferencingProperty);
}
#endif // WITH_VERSE_VM

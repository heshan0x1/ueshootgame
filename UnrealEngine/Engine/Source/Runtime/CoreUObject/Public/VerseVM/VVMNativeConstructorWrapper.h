// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#if WITH_VERSE_VM || defined(__INTELLISENSE__)

#include "VerseVM/VVMBitMap.h"
#include "VerseVM/VVMCell.h"
#include "VerseVM/VVMCreateFieldInlineCache.h"
#include "VerseVM/VVMGlobalTrivialEmergentTypePtr.h"
#include "VerseVM/VVMMap.h"

namespace Verse
{
struct VUniqueString;
struct VNativeStruct;

/*
  This is a wrapper around a native object for us to know what fields have been initialized or not in the object. Why?
  - For pure-Verse objects, we rely on our emergent type to track field initialization and can directly store placeholders for uninitialized fields.
  - Native objects don't have an emergent type and since the data lives in the `UObject` itself we can't directly store placeholders for uninitialized fields.

  To handle this, we wrap native objects in a `VNativeConstructorWrapper` object uses its emergent types bitmap of the fields
  created and placeholders for self/fields used before they are created. When we are done with archetype construction,
  we unify any placeholders then unwrap the native object using a special opcode and return it as part of `NewObject`.
  The wrapper object then gets GC'ed during the next collection.

  By convention, non-native Verse objects are not wrapped; the unwrap opcode just no-ops and returns the
  object itself when it encounters it (this is so we can avoid allocating an extra wrapper object in the common
  non-native case).

  Therefore, we always favour emitting the unwrap instruction where a native object needs to be unwrapped, since we
  don't know during codegen whether the object in question is native or not (since we do the wrapping in `NewObject` at
  runtime). Non-native objects can also be turned into native objects at any time (we test this as well using
  `--uobject-probability` in our tests.)
 */
struct VNativeConstructorWrapper : VCell
{
	DECLARE_DERIVED_VCPPCLASSINFO(COREUOBJECT_API, VCell);
	COREUOBJECT_API static TGlobalTrivialEmergentTypePtr<&StaticCppClassInfo> GlobalTrivialEmergentType;
	COREUOBJECT_API void AppendToStringImpl(FAllocationContext Context, FUtf8StringBuilderBase& Builder, EValueStringFormat Format, TSet<const void*>& VisitedObjects, uint32 RecursionDepth);

	COREUOBJECT_API static VNativeConstructorWrapper& New(FAllocationContext Context, VNativeStruct& ObjectToWrap);
	COREUOBJECT_API static VNativeConstructorWrapper& New(FAllocationContext Context, UObject& ObjectToWrap);

	bool CreateFieldCached(FAllocationContext Context, FCreateFieldCacheCase Cache);
	bool CreateField(FAllocationContext Context, VUniqueString& FieldName, FCreateFieldCacheCase* OutCacheCase = nullptr);

	bool IsFieldCreated(uint32 FieldIndex)
	{
		return GetEmergentType()->IsFieldCreated(FieldIndex);
	}

	VValue UnifyFieldPlaceholder(FAllocationContext Context, VUniqueString& FieldName)
	{
		if (!FieldPlaceholders)
		{
			return VValue();
		}
		VValue Placeholder = FieldPlaceholders->Find(Context, VInt(GetFieldIndex(Context, FieldName)));
		return Placeholder.Follow().IsPlaceholder() ? Placeholder : VValue();
	}

	// If `FieldName` is that of an uncreated field, returns an existing/new placeholder for it
	VValue LoadFieldPlaceholder(FAllocationContext Context, VUniqueString& FieldName)
	{
		int32 FieldIndex = GetFieldIndex(Context, FieldName);
		if (FieldIndex == -1 || IsFieldCreated(FieldIndex))
		{
			return VValue();
		}

		if (!FieldPlaceholders)
		{
			FieldPlaceholders.Set(Context, VMapBase::New<VMap>(Context, 1));
		}

		VValue Placeholder = FieldPlaceholders->Find(Context, VInt(FieldIndex));
		if (!Placeholder)
		{
			Placeholder = VValue::Placeholder(VPlaceholder::New(Context, 0));
			FieldPlaceholders->Add(Context, VInt(FieldIndex), Placeholder);
		}
		return Placeholder;
	}

	VValue WrappedObject() const;

	/// Placeholder for self that we pass to anything attempting to unwrap this before its fully created
	VRestValue SelfPlaceholder;

private:
	// returns:
	//  the index of the field if its created
	//  -1 if its not a field
	//  -2 if its an uncreated field
	int32 GetFieldIndex(FAllocationContext Context, VUniqueString& FieldName);

	VNativeConstructorWrapper(FAllocationContext Context, VNativeStruct& NativeStruct);
	VNativeConstructorWrapper(FAllocationContext Context, UObject& UEObject);

	/// This should either be a `VNativeStruct`/`UObject` wrapped in a `VValue`.
	TWriteBarrier<VValue> NativeObject;

	/// A map of indexes to placeholders tracking lenient uses of our wrapped object
	TWriteBarrier<VMapBase> FieldPlaceholders;

	friend class FInterpreter;
};

} // namespace Verse
#endif // WITH_VERSE_VM

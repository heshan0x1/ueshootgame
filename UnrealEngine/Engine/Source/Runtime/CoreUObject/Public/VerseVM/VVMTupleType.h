// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#if WITH_VERSE_VM || defined(__INTELLISENSE__)

#include "VerseVM/VVMGlobalTrivialEmergentTypePtr.h"
#include "VerseVM/VVMPackage.h"
#include "VerseVM/VVMType.h"
#include "VerseVM/VVMUniqueString.h"
#include "VerseVM/VVMVerseStruct.h"

class UVerseStruct;

namespace Verse
{

struct VProgram;
struct VPropertyType;

struct VTupleType : VType
{
	DECLARE_DERIVED_VCPPCLASSINFO(COREUOBJECT_API, VType);
	COREUOBJECT_API static TGlobalTrivialEmergentTypePtr<&StaticCppClassInfo> GlobalTrivialEmergentType;

	VUniqueString& GetMangledName() const { return *MangledName.Get(); }
	TArrayView<TWriteBarrier<VValue>> GetElements() { return {Elements, NumElements}; }

	UVerseStruct* GetOrCreateUStruct(FAllocationContext Context, UPackage* Package, bool bIsInstanced);
	COREUOBJECT_API void AddUStruct(FAllocationContext, UPackage* Package, UVerseStruct* Struct);

	COREUOBJECT_API static VTupleType& New(FAllocationContext Context, VProgram& Program, VUniqueString& InMangledName, int32 InNumElements, bool& bOutNew);

	static constexpr bool SerializeIdentity = false;
	static void SerializeLayout(FAllocationContext Context, VTupleType*& This, FStructuredArchiveVisitor& Visitor);
	void SerializeImpl(FAllocationContext Context, FStructuredArchiveVisitor& Visitor);

	COREUOBJECT_API TSharedPtr<FJsonValue> ToJSONImpl(FRunningContext Context, EValueJSONFormat Format, TMap<const void*, EVisitState>& VisitedObjects, const VerseVMToJsonCallback& Callback, uint32 RecursionDepth = 0, FJsonObject* Defs = nullptr);
	COREUOBJECT_API VValue FromJSONImpl(FRunningContext Context, const FJsonValue& JsonValue, EValueJSONFormat Format);

private:
	VTupleType(FAllocationContext Context, VUniqueString& InMangledName, int32 InNumElements)
		: VType(Context, &GlobalTrivialEmergentType.Get(Context))
		, MangledName(Context, InMangledName)
		, NumElements(InNumElements)
	{
		for (int32 Index = 0; Index < NumElements; ++Index)
		{
			new (&Elements[Index]) TWriteBarrier<VValue>();
		}
	}

	COREUOBJECT_API UVerseStruct* CreateUStruct(FAllocationContext Context, UPackage* Package, bool bIsInstanced);

	TWriteBarrier<VUniqueString> MangledName;

	struct FUStructMapKeyFuncs : TDefaultMapKeyFuncs<TWriteBarrier<VValue>, TWriteBarrier<VValue>, /*bInAllowDuplicateKeys*/ false>
	{
		static bool Matches(KeyInitType A, KeyInitType B) { return A == B; }
		static bool Matches(KeyInitType A, UPackage* B) { return A.Get() == B; }
		static uint32 GetKeyHash(KeyInitType Key) { return ::PointerHash(Key.Get().AsUObject()); }
		static uint32 GetKeyHash(UPackage* Key) { return ::PointerHash(Key); }
	};
	TMap<TWriteBarrier<VValue>, TWriteBarrier<VValue>, FDefaultSetAllocator, FUStructMapKeyFuncs> AssociatedUStructs;

	int32 NumElements;
	TWriteBarrier<VValue> Elements[];
};

inline UVerseStruct* VTupleType::GetOrCreateUStruct(FAllocationContext Context, UPackage* Package, bool bIsInstanced)
{
	if (TWriteBarrier<VValue>* Entry = AssociatedUStructs.Find({Context, Package}))
	{
		return CastChecked<UVerseStruct>(Entry->Get().AsUObject());
	}

	return CreateUStruct(Context, Package, bIsInstanced);
}

} // namespace Verse
#endif // WITH_VERSE_VM

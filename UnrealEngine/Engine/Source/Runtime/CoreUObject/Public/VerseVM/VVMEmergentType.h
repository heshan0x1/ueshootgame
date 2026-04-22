// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#if WITH_VERSE_VM || defined(__INTELLISENSE__)

#include "VerseVM/VVMBitMap.h"
#include "VerseVM/VVMCell.h"
#include "VerseVM/VVMEmergentTypeCreator.h"
#include "VerseVM/VVMGlobalHeapPtr.h"
#include <new>

namespace Verse
{
struct VShape;
struct VType;
struct VMap;

struct VEmergentType final : VCell
{
	DECLARE_DERIVED_VCPPCLASSINFO(COREUOBJECT_API, VCell);

	TWriteBarrier<VShape> Shape; // This is immutable. If you need to change an object's shape, transition to a new emergent type that points to your new shape instead.
	TWriteBarrier<VType> Type;
	TWriteBarrier<VEmergentType> MeltTransition;
	VCppClassInfo* CppClassInfo = nullptr;

	// We use this to track what fields have been created for objects
	VBitMap CreatedFields;

	// A cache of types made when calling `MarkFieldAsCreated`
	TWriteBarrier<VMap> CachedFieldTransitions;

	static VEmergentType* New(FAllocationContext Context, VType* Type, VCppClassInfo* CppClassInfo)
	{
		return new (Context.AllocateEmergentType(sizeof(VEmergentType))) VEmergentType(Context, VEmergentTypeCreator::EmergentTypeForEmergentType.Get(), Type, CppClassInfo);
	}

	static VEmergentType* New(FAllocationContext Context, VShape* InShape, VType* Type, VCppClassInfo* CppClassInfo)
	{
		return new (Context.AllocateEmergentType(sizeof(VEmergentType))) VEmergentType(Context, InShape, VEmergentTypeCreator::EmergentTypeForEmergentType.Get(), Type, CppClassInfo);
	}

	static VEmergentType* New(FAllocationContext Context, const VEmergentType* Other, const VBitMap* InCreatedFields)
	{
		return new (Context.AllocateEmergentType(sizeof(VEmergentType))) VEmergentType(Context, Other, InCreatedFields);
	}

	VEmergentType& GetOrCreateMeltTransition(FAllocationContext Context)
	{
		if (VEmergentType* Transition = MeltTransition.Get())
		{
			return *Transition;
		}
		return GetOrCreateMeltTransitionSlow(Context);
	}

	static bool Equals(const VEmergentType& EmergentType, VType* Type, VCppClassInfo* CppClassInfo)
	{
		return EmergentType.Shape.Get() == nullptr && EmergentType.Type.Get() == Type && EmergentType.CppClassInfo == CppClassInfo;
	}

	static bool Equals(const VEmergentType& EmergentType, const VShape* InShape, VType* Type, VCppClassInfo* CppClassInfo)
	{
		return EmergentType.Shape.Get() == InShape && EmergentType.Type.Get() == Type && EmergentType.CppClassInfo == CppClassInfo;
	}

	static bool Equals(FAllocationContext Context, const VEmergentType& EmergentType, const VEmergentType* Other, const VBitMap* InCreatedFields);

	friend uint32 GetTypeHash(const VEmergentType& EmergentType)
	{
		uint32 Hash = HashCombineFast(::GetTypeHash(EmergentType.Shape.Get()), ::GetTypeHash(EmergentType.Type.Get()));
		Hash = HashCombineFast(Hash, ::GetTypeHash(EmergentType.CppClassInfo));
		Hash = HashCombineFast(Hash, GetTypeHash(EmergentType.CreatedFields));
		return Hash;
	}

	COREUOBJECT_API bool IsFieldCreated(uint32 FieldIndex);
	COREUOBJECT_API VEmergentType* MarkFieldAsCreated(FAllocationContext Context, uint32 FieldIndex);

	// Emergent types are not serialized; this also prevents them from being recorded.
	static constexpr bool SerializeIdentity = false;

private:
	friend class VEmergentTypeCreator;

	// Need this for the EmergentType of EmergentType.
	static VEmergentType* NewIncomplete(FAllocationContext Context, VCppClassInfo* CppClassInfo)
	{
		return new (Context.AllocateEmergentType(sizeof(VEmergentType))) VEmergentType(Context, CppClassInfo);
	}

	VEmergentType& GetOrCreateMeltTransitionSlow(FAllocationContext);

	void SetEmergentType(FAccessContext Context, VEmergentType* EmergentType)
	{
		VCell::SetEmergentType(Context, EmergentType);
	}

	VEmergentType(FAllocationContext Context, VCppClassInfo* CppClassInfo)
		: VCell()
		, CppClassInfo(CppClassInfo)
	{
	}

	VEmergentType(FAllocationContext Context, VEmergentType* EmergentType, VType* T, VCppClassInfo* CppClassInfo)
		: VCell(Context, EmergentType)
		, Type(Context, T)
		, CppClassInfo(CppClassInfo)
	{
	}

	VEmergentType(FAllocationContext Context, VShape* InShape, VEmergentType* EmergentType, VType* InType, VCppClassInfo* InCppClassInfo);

	VEmergentType(FAllocationContext Context, const VEmergentType* Other, const VBitMap* InCreatedFields);
};

}; // namespace Verse

#endif // WITH_VERSE_VM

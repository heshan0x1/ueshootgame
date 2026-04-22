// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#if WITH_VERSE_VM || defined(__INTELLISENSE__)

#include "VerseVM/VVMObject.h"

namespace Verse
{
enum class ECompares : uint8;
struct VClass;

template <typename CppStructType>
VClass& StaticVClass();

/// A variant of Verse object that boxes a native (C++ defined) struct
struct VNativeStruct : VObject
{
	DECLARE_DERIVED_VCPPCLASSINFO(COREUOBJECT_API, VObject);
	COREUOBJECT_API static TGlobalTrivialEmergentTypePtr<&StaticCppClassInfo> GlobalTrivialEmergentType;

	template <class CppStructType>
	CppStructType& GetStruct();
	void* GetStruct();

	static UScriptStruct* GetUScriptStruct(VEmergentType& EmergentType);

	/// Allocate a new VNativeStruct and move an existing struct into it
	template <class CppStructType>
	static VNativeStruct& New(FAllocationContext Context, VEmergentType& InEmergentType, CppStructType&& InStruct);

	/// Allocate a new blank VNativeStruct
	static VNativeStruct& NewUninitialized(FAllocationContext Context, VEmergentType& InEmergentType);

protected:
	friend class FInterpreter;

	static std::byte* AllocateCell(FAllocationContext Context, size_t Size, bool bHasDestructor);

	template <class CppStructType>
	VNativeStruct(FAllocationContext Context, VEmergentType& InEmergentType, CppStructType&& InStruct);
	VNativeStruct(FAllocationContext Context, VEmergentType& InEmergentType);
	VNativeStruct(FAllocationContext Context);
	~VNativeStruct();

	VNativeStruct& Duplicate(FAllocationContext Context);

	ECompares EqualImpl(FAllocationContext Context, VCell* Other, const TFunction<void(::Verse::VValue, ::Verse::VValue)>& HandlePlaceholder);
	uint32 GetTypeHashImpl();
	VValue MeltImpl(FAllocationContext Context);
	FOpResult FreezeImpl(FAllocationContext Context, VTask*, FOp* AwaitPC);
	static void SerializeLayout(FAllocationContext, VNativeStruct*&, FStructuredArchiveVisitor&);
	void SerializeImpl(FAllocationContext, FStructuredArchiveVisitor&);
};

} // namespace Verse
#endif // WITH_VERSE_VM

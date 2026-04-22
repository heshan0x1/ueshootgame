// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#if WITH_VERSE_VM || defined(__INTELLISENSE__)

#include "VVMClass.h"
#include "VVMGlobalTrivialEmergentTypePtr.h"
#include "VVMWriteBarrier.h"

namespace Verse
{
/// The lexical scope of a function.
struct VScope : VCell
{
	DECLARE_DERIVED_VCPPCLASSINFO(COREUOBJECT_API, VCell);
	COREUOBJECT_API static TGlobalTrivialEmergentTypePtr<&StaticCppClassInfo> GlobalTrivialEmergentType;

	TWriteBarrier<VScope> ParentScope;

	// Variables free in this scope but not free in `ParentScope`.
	const uint32 NumCaptures;
	TWriteBarrier<VValue> Captures[];

	static VScope& New(FAllocationContext, VScope* ParentScope, uint32 NumCaptures);
	static VScope& New(FAllocationContext, VClass* SuperClass);
	static VScope& New(FAllocationContext Context);
	static VScope& NewUninitialized(FAllocationContext, uint32 NumCaptures);

	VScope& GetRootScope();

	static void SerializeLayout(FAllocationContext, VScope*& This, FStructuredArchiveVisitor&);
	void SerializeImpl(FAllocationContext, FStructuredArchiveVisitor&);

private:
	static size_t NumBytes(uint32 NumCaptures);

	VScope(FAllocationContext, VScope* ParentScope, uint32 NumCaptures);
};
} // namespace Verse
#endif // WITH_VERSE_VM

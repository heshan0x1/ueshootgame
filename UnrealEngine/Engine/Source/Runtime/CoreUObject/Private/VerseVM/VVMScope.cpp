// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_VERSE_VM || defined(__INTELLISENSE__)
#include "VerseVM/VVMScope.h"
#include "VerseVM/Inline/VVMCellInline.h"
#include "VerseVM/Inline/VVMScopeInline.h"
#include "VerseVM/VVMStructuredArchiveVisitor.h"

namespace Verse
{
DEFINE_DERIVED_VCPPCLASSINFO(VScope)
TGlobalTrivialEmergentTypePtr<&VScope::StaticCppClassInfo> VScope::GlobalTrivialEmergentType;

template <typename TVisitor>
void VScope::VisitReferencesImpl(TVisitor& Visitor)
{
	Visitor.Visit(ParentScope, TEXT("ParentScope"));
	Visitor.Visit(Captures, Captures + NumCaptures, TEXT("Captures"));
}

void VScope::SerializeLayout(FAllocationContext Context, VScope*& This, FStructuredArchiveVisitor& Visitor)
{
	uint32 NumCaptures = 0;
	if (!Visitor.IsLoading())
	{
		NumCaptures = This->NumCaptures;
	}
	Visitor.Visit(NumCaptures, TEXT("NumCaptures"));
	if (Visitor.IsLoading())
	{
		This = &VScope::NewUninitialized(Context, NumCaptures);
	}
}

void VScope::SerializeImpl(FAllocationContext Context, FStructuredArchiveVisitor& Visitor)
{
	Visitor.Visit(ParentScope, TEXT("ParentScope"));
	Visitor.Visit(Captures, Captures + NumCaptures, TEXT("Captures"));
}
} // namespace Verse

#endif

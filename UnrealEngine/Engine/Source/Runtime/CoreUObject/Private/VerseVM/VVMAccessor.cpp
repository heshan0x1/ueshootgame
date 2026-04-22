// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_VERSE_VM || defined(__INTELLISENSE__)
#include "VerseVM/VVMAccessor.h"
#include "VerseVM/Inline/VVMCellInline.h"
#include "VerseVM/VVMCppClassInfo.h"
#include "VerseVM/VVMStructuredArchiveVisitor.h"

namespace Verse
{

DEFINE_DERIVED_VCPPCLASSINFO(VAccessor);
TGlobalTrivialEmergentTypePtr<&VAccessor::StaticCppClassInfo> VAccessor::GlobalTrivialEmergentType;

template <typename TVisitor>
void VAccessor::VisitReferencesImpl(TVisitor& Visitor)
{
	Visitor.Visit(GetGettersBegin(), GetGettersEnd(), TEXT("Getters"));
	Visitor.Visit(GetSettersBegin(), GetSettersEnd(), TEXT("Setters"));
}

void VAccessor::SerializeLayout(FAllocationContext Context, VAccessor*& This, FStructuredArchiveVisitor& Visitor)
{
	uint32 NumAccessors = 0;
	if (!Visitor.IsLoading())
	{
		NumAccessors = This->NumAccessors;
	}
	Visitor.Visit(NumAccessors, TEXT("NumAccessors"));
	if (Visitor.IsLoading())
	{
		This = &VAccessor::NewUninitialized(Context, NumAccessors);
	}
}

void VAccessor::SerializeImpl(FAllocationContext Context, FStructuredArchiveVisitor& Visitor)
{
	Visitor.Visit(GetGettersBegin(), GetGettersEnd(), TEXT("Getters"));
	Visitor.Visit(GetSettersBegin(), GetSettersEnd(), TEXT("Setters"));
}

DEFINE_DERIVED_VCPPCLASSINFO(VAccessChain);
TGlobalTrivialEmergentTypePtr<&VAccessChain::StaticCppClassInfo> VAccessChain::GlobalTrivialEmergentType;

TGlobalHeapPtr<VEnumerator> VAccessChain::AccessorEnum;

template <typename TVisitor>
void VAccessChain::VisitReferencesImpl(TVisitor& Visitor)
{
	Visitor.Visit(Accessor, TEXT("Accessor"));
	Visitor.Visit(Self, TEXT("Self"));
	Visitor.Visit(GetChainBegin(), GetChainEnd(), TEXT("Chain"));
}

} // namespace Verse
#endif // WITH_VERSE_VM || defined(__INTELLISENSE__)
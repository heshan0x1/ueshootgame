// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_VERSE_VM || defined(__INTELLISENSE__)

#include "VerseVM/VVMFastFailureContext.h"
#include "VerseVM/Inline/VVMAbstractVisitorInline.h"
#include "VerseVM/Inline/VVMCellInline.h"
#include "VerseVM/Inline/VVMMarkStackVisitorInline.h"
#include "VerseVM/VVMAbstractVisitor.h"
#include "VerseVM/VVMCppClassInfo.h"

namespace Verse
{

DEFINE_DERIVED_VCPPCLASSINFO(VFastFailureContext);
TGlobalTrivialEmergentTypePtr<&VFastFailureContext::StaticCppClassInfo> VFastFailureContext::GlobalTrivialEmergentType;

template <typename TVisitor>
void VFastFailureContext::VisitReferencesImpl(TVisitor& Visitor)
{
	Visitor.Visit(Parent, TEXT("Parent"));
	Visitor.Visit(CapturedFrame, TEXT("CapturedFrame"));
}

} // namespace Verse
#endif // WITH_VERSE_VM

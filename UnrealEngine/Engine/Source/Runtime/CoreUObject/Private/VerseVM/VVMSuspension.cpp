// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_VERSE_VM || defined(__INTELLISENSE__)
#include "VerseVM/VVMSuspension.h"
#include "VerseVM/Inline/VVMCellInline.h"
#include "VerseVM/Inline/VVMMarkStackVisitorInline.h"
#include "VerseVM/VVMBytecodesAndCaptures.h"
#include "VerseVM/VVMCaptureSwitch.h"
#include "VerseVM/VVMCppClassInfo.h"
#include "VerseVM/VVMFailureContext.h"
#include "VerseVM/VVMInstantiationContext.h"
#include "VerseVM/VVMPackage.h"
#include "VerseVM/VVMProcedure.h"
#include "VerseVM/VVMTask.h"

namespace Verse
{

DEFINE_DERIVED_VCPPCLASSINFO(VSuspension);

template <typename TVisitor>
void VSuspension::VisitReferencesImpl(TVisitor& Visitor)
{
	Visitor.Visit(FailureContext, TEXT("FailureContext"));
	Visitor.Visit(Task, TEXT("Task"));
	Visitor.Visit(Next, TEXT("Next"));

#if WITH_EDITORONLY_DATA
	Visitor.Visit(CurrentPackage, TEXT("CurrentPackage"));
#endif
	Visitor.Visit(CurrentOuter, TEXT("CurrentOuter"));
}

VSuspension::VSuspension(FAllocationContext Context, VEmergentType* EmergentType, VFailureContext* FailureContext, VTask* Task)
	: VCell(Context, EmergentType)
	, FailureContext(Context, FailureContext)
	, Task(Context, Task)
#if WITH_EDITORONLY_DATA
	, CurrentPackage(Context, Context.GetCurrentPackage())
#endif
	, CurrentOuter(Context, FInstantiationScope::Context.Outer)
	, CurrentFlags(FInstantiationScope::Context.Flags)
{
}

DEFINE_DERIVED_VCPPCLASSINFO(VBytecodeSuspension);
TGlobalTrivialEmergentTypePtr<&VBytecodeSuspension::StaticCppClassInfo> VBytecodeSuspension::GlobalTrivialEmergentType;

template <typename TVisitor>
void VBytecodeSuspension::VisitReferencesImpl(TVisitor& Visitor)
{
	Visitor.Visit(Procedure, TEXT("Procedure"));
	CaptureSwitch([&Visitor](auto& Captures) {
		Captures.ForEachOperand([&Visitor](EOperandRole, auto& Value, const TCHAR* Name) {
			Visitor.Visit(Value, Name);
		});
	});
}

template <typename Captures>
static void DestroyCaptures(Captures& TheCaptures)
{
	TheCaptures.~Captures();
}

VBytecodeSuspension::~VBytecodeSuspension()
{
	CaptureSwitch([](auto& Captures) {
		DestroyCaptures(Captures);
	});
}

DEFINE_DERIVED_VCPPCLASSINFO(VLambdaSuspension);
TGlobalTrivialEmergentTypePtr<&VLambdaSuspension::StaticCppClassInfo> VLambdaSuspension::GlobalTrivialEmergentType;

template <typename TVisitor>
void VLambdaSuspension::VisitReferencesImpl(TVisitor& Visitor)
{
	Visitor.Visit(Args(), NumValues, TEXT("Values"));
}

} // namespace Verse
#endif // WITH_VERSE_VM || defined(__INTELLISENSE__)

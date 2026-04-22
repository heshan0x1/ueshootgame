// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_VERSE_VM || defined(__INTELLISENSE__)
#include "VerseVM/VVMEnumerator.h"
#include "Templates/TypeHash.h"
#include "VerseVM/Inline/VVMArrayBaseInline.h"
#include "VerseVM/Inline/VVMCellInline.h"
#include "VerseVM/VVMCppClassInfo.h"
#include "VerseVM/VVMEnumeration.h"
#include "VerseVM/VVMJson.h"
#include "VerseVM/VVMStructuredArchiveVisitor.h"
#include "VerseVM/VVMTextPrinting.h"
#include "VerseVM/VVMUniqueString.h"
#include "VerseVM/VVMValuePrinting.h"

namespace Verse
{

DEFINE_DERIVED_VCPPCLASSINFO(VEnumerator);
TGlobalTrivialEmergentTypePtr<&VEnumerator::StaticCppClassInfo> VEnumerator::GlobalTrivialEmergentType;

template <typename TVisitor>
void VEnumerator::VisitReferencesImpl(TVisitor& Visitor)
{
	Visitor.Visit(Enumeration, TEXT("Enumeration"));
	Visitor.Visit(Name, TEXT("Name"));
}

uint32 VEnumerator::GetTypeHashImpl()
{
	return PointerHash(this);
}

void VEnumerator::AppendToStringImpl(FAllocationContext Context, FUtf8StringBuilderBase& Builder, EValueStringFormat Format, TSet<const void*>& VisitedObjects, uint32 RecursionDepth)
{
	if (VEnumeration* MyEnumeration = GetEnumeration())
	{
		MyEnumeration->AppendQualifiedName(Builder);
	}
	Builder << UTF8TEXT('.');
	Builder << GetName()->AsStringView();
}

TSharedPtr<FJsonValue> VEnumerator::ToJSONImpl(FRunningContext Context, EValueJSONFormat Format, TMap<const void*, EVisitState>& VisitedObjects, const VerseVMToJsonCallback& Callback, uint32 RecursionDepth, FJsonObject* Defs)
{
	FUtf8String String{GetEnumeration()->GetFullName()};
	String += "::";
	VUniqueString* ShortName = GetName();
	String.Append(ShortName->GetData<UTF8CHAR>(), ShortName->Num());
	return MakeShared<TJsonValueString<UTF8CHAR>>(::MoveTemp(String));
}

void VEnumerator::SerializeLayout(FAllocationContext Context, VEnumerator*& This, FStructuredArchiveVisitor& Visitor)
{
	if (Visitor.IsLoading())
	{
		This = &VEnumerator::New(Context, nullptr, 0);
	}
}

void VEnumerator::SerializeImpl(FAllocationContext Context, FStructuredArchiveVisitor& Visitor)
{
	Visitor.Visit(Enumeration, TEXT("Enumeration"));
	Visitor.Visit(Name, TEXT("Name"));
	Visitor.Visit(IntValue, TEXT("IntValue"));
}

} // namespace Verse
#endif // WITH_VERSE_VM || defined(__INTELLISENSE__)

// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_VERSE_VM || defined(__INTELLISENSE__)
#include "VerseVM/VVMOption.h"
#include "VerseVM/Inline/VVMValueInline.h"
#include "VerseVM/VVMCppClassInfo.h"
#include "VerseVM/VVMDebuggerVisitor.h"
#include "VerseVM/VVMFalse.h"
#include "VerseVM/VVMJson.h"
#include "VerseVM/VVMStructuredArchiveVisitor.h"
#include "VerseVM/VVMValue.h"
#include "VerseVM/VVMValuePrinting.h"

namespace Verse
{

DEFINE_DERIVED_VCPPCLASSINFO(VOption);
TGlobalTrivialEmergentTypePtr<&VOption::StaticCppClassInfo> VOption::GlobalTrivialEmergentType;

template <typename TVisitor>
void VOption::VisitReferencesImpl(TVisitor& Visitor)
{
	Visitor.Visit(Value, TEXT("Value"));
}

uint32 VOption::GetTypeHashImpl()
{
	static constexpr uint32 MagicNumber = 0x9e3779b9;
	return ::HashCombineFast(static_cast<uint32>(MagicNumber), GetTypeHash(GetValue()));
}

void VOption::VisitMembersImpl(FAllocationContext Context, FDebuggerVisitor& Visitor)
{
	if (this == &GlobalTrue())
	{
		return;
	}

	Visitor.VisitOption([this, &Visitor] {
		Visitor.Visit(GetValue(), "");
	});
}

void VOption::AppendToStringImpl(FAllocationContext Context, FUtf8StringBuilderBase& Builder, EValueStringFormat Format, TSet<const void*>& VisitedObjects, uint32 RecursionDepth)
{
	if (this == &GlobalTrue())
	{
		Builder.Append(UTF8TEXT("true"));
	}
	else
	{
		if (!IsCellFormat(Format))
		{
			Builder << UTF8TEXT("option{");
		}

		GetValue().AppendToString(Context, Builder, Format, VisitedObjects, RecursionDepth + 1);

		if (!IsCellFormat(Format))
		{
			Builder << UTF8TEXT('}');
		}
	}
}

TSharedPtr<FJsonValue> VOption::ToJSONImpl(FRunningContext Context, EValueJSONFormat Format, TMap<const void*, EVisitState>& VisitedObjects, const VerseVMToJsonCallback& Callback, uint32 RecursionDepth, FJsonObject* Defs)
{
	if (this == &GlobalTrue())
	{
		return MakeShared<FJsonValueBoolean>(true);
	}
	return Wrap(GetValue().ToJSON(Context, Format, VisitedObjects, Callback, RecursionDepth, Defs), Format);
}

void VOption::SerializeLayout(FAllocationContext Context, VOption*& This, FStructuredArchiveVisitor& Visitor)
{
	bool bTrue = false;
	if (!Visitor.IsLoading())
	{
		bTrue = This == &GlobalTrue();
	}

	Visitor.Visit(bTrue, TEXT("True"));
	if (Visitor.IsLoading())
	{
		This = bTrue ? &GlobalTrue() : &VOption::New(Context, VValue());
	}
}

void VOption::SerializeImpl(FAllocationContext Context, FStructuredArchiveVisitor& Visitor)
{
	if (this == &GlobalTrue())
	{
		return;
	}

	Visitor.Visit(Value, TEXT("Value"));
}

} // namespace Verse
#endif // WITH_VERSE_VM || defined(__INTELLISENSE__)
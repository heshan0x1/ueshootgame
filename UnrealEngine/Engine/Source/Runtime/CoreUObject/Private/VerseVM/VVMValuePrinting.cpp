// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_VERSE_VM || defined(__INTELLISENSE__)

#include "VerseVM/VVMValuePrinting.h"
#include "Containers/UnrealString.h"
#include "VerseVM/Inline/VVMCellInline.h"
#include "VerseVM/Inline/VVMIntInline.h"
#include "VerseVM/Inline/VVMUniqueStringInline.h"
#include "VerseVM/Inline/VVMValueInline.h"
#include "VerseVM/VVMClass.h"
#include "VerseVM/VVMEmergentType.h"
#include "VerseVM/VVMFloatPrinting.h"
#include "VerseVM/VVMFunction.h"
#include "VerseVM/VVMInt.h"
#include "VerseVM/VVMLog.h"
#include "VerseVM/VVMObjectPrinting.h"
#include "VerseVM/VVMPlaceholder.h"
#include "VerseVM/VVMProcedure.h"
#include "VerseVM/VVMRational.h"
#include "VerseVM/VVMRef.h"
#include "VerseVM/VVMRestValue.h"
#include "VerseVM/VVMTextPrinting.h"

#include <cinttypes>

namespace Verse
{
void VInt::AppendDecimalToString(FUtf8StringBuilderBase& Builder, FAllocationContext Context) const
{
	if (IsInt64())
	{
		const int64 Int64 = AsInt64();
		Builder << Int64;
	}
	else
	{
		StaticCast<VHeapInt>().AppendDecimalToString(Builder, Context);
	}
}

void VInt::AppendHexToString(FUtf8StringBuilderBase& Builder) const
{
	if (IsInt64())
	{
		const int64 Int64 = AsInt64();
		if (Int64 < 0)
		{
			Builder.Appendf(UTF8TEXT("-0x%x"), -Int64);
		}
		else
		{
			Builder.Appendf(UTF8TEXT("0x%x"), Int64);
		}
	}
	else
	{
		StaticCast<VHeapInt>().AppendHexToString(Builder);
	}
}

void VValue::AppendToString(FAllocationContext Context, FUtf8StringBuilderBase& Builder, EValueStringFormat Format, TSet<const void*>& VisitedObjects, uint32 RecursionDepth) const
{
	// Don't deeply recurse into data structures to guard against reference cycles.
	if (RecursionDepth > 10)
	{
		Builder << UTF8TEXT("\"...\"");
		return;
	}

	if (IsInt())
	{
		if (IsCellFormat(Format) && (*this == VValue::EffectDoneMarker() || *this == VValue::CreateFieldMarker() || *this == VValue::ConstructedMarker()))
		{
			AsInt().AppendHexToString(Builder);
		}
		else
		{
			AsInt().AppendDecimalToString(Builder, Context);
		}
	}
	else if (IsCell())
	{
		AsCell().AppendToString(Context, Builder, Format, VisitedObjects, RecursionDepth);
	}
	else if (VRef* Ref = ExtractTransparentRef())
	{
		if (Format != EValueStringFormat::VerseSyntax)
		{
			Builder.Append(UTF8TEXT("TransparentRef("));
		}
		Ref->AppendToStringImpl(Context, Builder, Format, VisitedObjects, RecursionDepth);
		if (Format != EValueStringFormat::VerseSyntax)
		{
			Builder.Append(UTF8TEXT(")"));
		}
	}
	else if (IsUObject())
	{
		::Verse::AppendToString(Builder, AsUObject(), Format, VisitedObjects, RecursionDepth);
	}
	else if (IsPlaceholder())
	{
		VPlaceholder& Placeholder = AsPlaceholder();
		VValue Pointee = Placeholder.Follow();
		if (Format == EValueStringFormat::VerseSyntax || Format == EValueStringFormat::Diagnostic)
		{
			if (Pointee.IsPlaceholder())
			{
				Builder.Append(UTF8TEXT("_"));
			}
			else
			{
				Pointee.AppendToString(Context, Builder, Format, VisitedObjects, RecursionDepth + 1);
			}
		}
		else
		{
			Builder.Appendf(UTF8TEXT("Placeholder(0x%" PRIxPTR "->"), &Placeholder);
			if (Pointee.IsPlaceholder())
			{
				Builder.Appendf(UTF8TEXT("0x%" PRIxPTR), &Pointee.AsPlaceholder());
			}
			else
			{
				Pointee.AppendToString(Context, Builder, Format, VisitedObjects, RecursionDepth + 1);
			}
			Builder.Append(TEXT(")"));
		}
	}
	else
	{
		if (IsFloat())
		{
			AppendDecimalToString(Builder, AsFloat());
		}
		else if (IsChar())
		{
			AppendVerseToString(Builder, AsChar());
		}
		else if (IsChar32())
		{
			AppendVerseToString(Builder, AsChar32());
		}
		else if (IsRoot())
		{
			Builder.Appendf(UTF8TEXT("Root(%u)"), GetSplitDepth());
		}
		else if (IsUninitialized())
		{
			Builder.Append(UTF8TEXT("Uninitialized"));
		}
		else
		{
			V_DIE("Unhandled Verse value encoding: 0x%" PRIxPTR, GetEncodedBits());
		}
	}
}

void VRestValue::AppendToString(FAllocationContext Context, FUtf8StringBuilderBase& Builder, EValueStringFormat Format, TSet<const void*>& VisitedObjects, uint32 RecursionDepth) const
{
	Value.Get().AppendToString(Context, Builder, Format, VisitedObjects, RecursionDepth);
}

void VCell::AppendToString(FAllocationContext Context, FUtf8StringBuilderBase& Builder, EValueStringFormat Format, TSet<const void*>& VisitedObjects, uint32 RecursionDepth)
{
	// Don't deeply recurse into data structures to guard against reference cycles.
	if (RecursionDepth > 10)
	{
		Builder << UTF8TEXT("\"...\"");
		return;
	}

	// Logical values are handled via two globally unique cells.
	// For concision, the cell formats omit the C++ class name to match the other formats as a special case.
	if (this == &GlobalFalse())
	{
		Builder << UTF8TEXT("false");
		return;
	}
	else if (this == &GlobalTrue())
	{
		Builder << UTF8TEXT("true");
		return;
	}
	else if (IsA<VVoidType>())
	{
		Builder << UTF8TEXT("void");
		return;
	}

	VEmergentType* EmergentType = GetEmergentType();

	if (IsCellFormat(Format))
	{
		FString DebugName = EmergentType->CppClassInfo->DebugName();
		FStringView DebugNameWithoutV = DebugName;
		if (DebugName[0] == TEXT('V'))
		{
			DebugNameWithoutV = DebugNameWithoutV.Mid(1);
		}
		Builder << DebugNameWithoutV;
		if (Format == EValueStringFormat::CellsWithAddresses)
		{
			Builder.Appendf(UTF8TEXT("@0x%p"), this);
		}
		Builder << UTF8TEXT('(');
	}

	EmergentType->CppClassInfo->AppendToString(Context, this, Builder, Format, VisitedObjects, RecursionDepth);

	if (IsCellFormat(Format))
	{
		Builder << UTF8TEXT(')');
	}
}

void VCell::AppendToStringImpl(FAllocationContext Context, FUtf8StringBuilderBase& Builder, EValueStringFormat Format, TSet<const void*>& VisitedObjects, uint32 RecursionDepth)
{
	Builder << UTF8TEXT("\"");
	Builder << GetEmergentType()->CppClassInfo->DebugName();
	Builder << UTF8TEXT("\{}\"");
}

void AppendToString(VCell* Cell, FUtf8StringBuilderBase& Builder, FAllocationContext Context, EValueStringFormat Format, TSet<const void*>& VisitedObjects, uint32 RecursionDepth)
{
	Cell->AppendToString(Context, Builder, Format, VisitedObjects, RecursionDepth);
}

void AppendToString(VValue Value, FUtf8StringBuilderBase& Builder, FAllocationContext Context, EValueStringFormat Format, TSet<const void*>& VisitedObjects, uint32 RecursionDepth)
{
	Value.AppendToString(Context, Builder, Format, VisitedObjects, RecursionDepth);
}

FUtf8String VValue::ToString(FAllocationContext Context, EValueStringFormat Format, uint32 RecursionDepth) const
{
	TUtf8StringBuilder<64> Builder;
	TSet<const void*> VisitedObjects;
	AppendToString(Context, Builder, Format, VisitedObjects, RecursionDepth);
	return FUtf8String(Builder);
}

FUtf8String VRestValue::ToString(FAllocationContext Context, EValueStringFormat Format, uint32 RecursionDepth) const
{
	return Value.Get().ToString(Context, Format, RecursionDepth);
}

FUtf8String VCell::ToString(FAllocationContext Context, EValueStringFormat Format, uint32 RecursionDepth)
{
	TUtf8StringBuilder<64> Builder;
	TSet<const void*> VisitedObjects;
	AppendToString(Context, Builder, Format, VisitedObjects, RecursionDepth);
	return FUtf8String(Builder);
}

FUtf8String ToString(VCell* Cell, FAllocationContext Context, EValueStringFormat Format, uint32 RecursionDepth)
{
	TUtf8StringBuilder<64> Builder;
	TSet<const void*> VisitedObjects;
	Cell->AppendToString(Context, Builder, Format, VisitedObjects, RecursionDepth);
	return FUtf8String(Builder);
}

FUtf8String ToString(VValue Value, FAllocationContext Context, EValueStringFormat Format, uint32 RecursionDepth)
{
	TUtf8StringBuilder<64> Builder;
	TSet<const void*> VisitedObjects;
	Value.AppendToString(Context, Builder, Format, VisitedObjects, RecursionDepth);
	return FUtf8String(Builder);
}
} // namespace Verse

#endif // WITH_VERSE_VM || defined(__INTELLISENSE__)

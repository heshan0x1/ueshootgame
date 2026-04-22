// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_VERSE_VM || defined(__INTELLISENSE__)

#include "VerseVM/VVMFloatType.h"
#include "VerseVM/Inline/VVMCellInline.h"
#include "VerseVM/VVMAbstractVisitor.h"
#include "VerseVM/VVMFloatPrinting.h"
#include "VerseVM/VVMJson.h"
#include "VerseVM/VVMStructuredArchiveVisitor.h"
#include "VerseVM/VVMValuePrinting.h"

namespace Verse
{
DEFINE_DERIVED_VCPPCLASSINFO(VFloatType);
DEFINE_TRIVIAL_VISIT_REFERENCES(VFloatType);
TGlobalTrivialEmergentTypePtr<&VFloatType::StaticCppClassInfo> VFloatType::GlobalTrivialEmergentType;

bool VFloatType::SubsumesImpl(FAllocationContext Context, VValue Value)
{
	if (!Value.IsFloat())
	{
		return false;
	}

	VFloat Float = Value.AsFloat();
	return GetMin() <= Float && (GetMax().IsNaN() || GetMax() >= Float);
}

void VFloatType::AppendToStringImpl(FAllocationContext Context, FUtf8StringBuilderBase& Builder, EValueStringFormat Format, TSet<const void*>& VisitedObjects, uint32 RecursionDepth)
{
	const bool bUnconstrainedMin = GetMin() == -VFloat::Infinity();
	// If there is a lower bound, then NaN is already excluded, and we can treat NaN and +Infinity as equivalently unconstrained upper bounds.
	const bool bUnconstrainedMax = GetMax().IsNaN() || (GetMax().IsInfinite() && !bUnconstrainedMin);
	if (bUnconstrainedMin && bUnconstrainedMax)
	{
		Builder << UTF8TEXT("float");
	}
	else if (bUnconstrainedMin)
	{
		Builder << UTF8TEXT("type{:float<=");
		AppendDecimalToString(Builder, GetMax());
		Builder << UTF8TEXT('}');
	}
	else if (bUnconstrainedMax)
	{
		Builder << UTF8TEXT("type{:float>=");
		AppendDecimalToString(Builder, GetMin());
		Builder << UTF8TEXT('}');
	}
	else if (GetMin() == GetMax())
	{
		Builder << UTF8TEXT("type{");
		AppendDecimalToString(Builder, GetMin());
		Builder << UTF8TEXT('}');
	}
	else
	{
		Builder << UTF8TEXT("type{:float>=");
		AppendDecimalToString(Builder, GetMin());
		Builder << UTF8TEXT("<=");
		AppendDecimalToString(Builder, GetMax());
		Builder << UTF8TEXT('}');
	}
}

TSharedPtr<FJsonValue> VFloatType::ToJSONImpl(FRunningContext Context, EValueJSONFormat Format, TMap<const void*, EVisitState>& VisitedObjects, const VerseVMToJsonCallback& Callback, uint32 RecursionDepth, FJsonObject* Defs)
{
	const bool bUnconstrainedMin = GetMin() == -VFloat::Infinity();
	// If there is a lower bound, then NaN is already excluded, and we can treat NaN and +Infinity as equivalently unconstrained upper bounds.
	const bool bUnconstrainedMax = GetMax().IsNaN() || (GetMax().IsInfinite() && !bUnconstrainedMin);
	TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
	Object->SetStringField(PERSONA_FIELD(Type), Persona::NumberString);
	if (!bUnconstrainedMin)
	{
		Object->SetField(PERSONA_FIELD(Minimum), MakeShared<FJsonValueNumber>(GetMin().AsDouble()));
	}
	if (!bUnconstrainedMax)
	{
		Object->SetField(PERSONA_FIELD(Maximum), MakeShared<FJsonValueNumber>(GetMax().AsDouble()));
	}
	return MakeShared<FJsonValueObject>(Object);
}

VValue VFloatType::FromJSONImpl(FRunningContext Context, const FJsonValue& JsonValue, EValueJSONFormat Format)
{
	double DoubleValue;
	if (!JsonValue.TryGetNumber(DoubleValue))
	{
		return VValue();
	}
	VFloat Result = VFloat(DoubleValue);
	if (Result < GetMin() || Result > GetMax())
	{
		return VValue();
	}
	return Result;
	return VValue();
}

void VFloatType::SerializeLayout(FAllocationContext Context, VFloatType*& This, FStructuredArchiveVisitor& Visitor)
{
	if (Visitor.IsLoading())
	{
		This = &VFloatType::New(Context, VFloat(), VFloat());
	}
}

void VFloatType::SerializeImpl(FAllocationContext, FStructuredArchiveVisitor& Visitor)
{
	double ScratchMin = Min.AsDouble();
	double ScratchMax = Max.AsDouble();
	Visitor.Visit(ScratchMin, TEXT("Min"));
	Visitor.Visit(ScratchMax, TEXT("Max"));
	if (Visitor.IsLoading())
	{
		Min = VFloat(ScratchMin);
		Max = VFloat(ScratchMax);
	}
}

} // namespace Verse

#endif
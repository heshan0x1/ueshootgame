// Copyright Epic Games, Inc. All Rights Reserved.

#include "VerseVM/VVMJson.h"
#include "Containers/UnrealString.h"
#include "Dom/JsonObject.h"
#include "VerseVM/Inline/VVMCellInline.h"
#include "VerseVM/Inline/VVMIntInline.h"
#include "VerseVM/Inline/VVMUniqueStringInline.h"
#include "VerseVM/Inline/VVMValueInline.h"
#include "VerseVM/VVMClass.h"
#include "VerseVM/VVMEmergentType.h"
#include "VerseVM/VVMFloatPrinting.h"
#include "VerseVM/VVMInt.h"
#include "VerseVM/VVMObject.h"
#include "VerseVM/VVMObjectPrinting.h"
#include "VerseVM/VVMPlaceholder.h"
#include "VerseVM/VVMRestValue.h"
#include "VerseVM/VVMRuntimeError.h"
#include "VerseVM/VVMValue.h"
#include "VerseVM/VVMValuePrinting.h"

namespace Verse
{
TSharedRef<FJsonValue> Int64ToJson(int64 Arg)
{
	return MakeShared<FJsonValueNumberString>(LexToString(Arg));
}

bool TryGetInt64(const FJsonValue& JsonValue, int64& Int64Value)
{
	if (JsonValue.TryGetNumber(Int64Value))
	{
		return true;
	}
	// If near the max int64 value is written out as a double, one more than the
	// max int64 value may be written.  Upon deserialization, the bounds check
	// done by `TryGetNumber` will fail.  Try again using a `double`.
	double DoubleValue;
	if (!JsonValue.TryGetNumber(DoubleValue))
	{
		return false;
	}
	if (int64 MinInt64Value = std::numeric_limits<std::int64_t>::min(); DoubleValue < static_cast<double>(MinInt64Value))
	{
		Int64Value = MinInt64Value;
	}
	else if (int64 MaxInt64Value = std::numeric_limits<int64>::max(); DoubleValue >= static_cast<double>(MaxInt64Value))
	{
		Int64Value = MaxInt64Value;
	}
	else
	{
		Int64Value = static_cast<int64>(DoubleValue);
	}
	return true;
}

TSharedPtr<FJsonValue> Wrap(const TSharedPtr<FJsonValue>& Value, EValueJSONFormat Format)
{
	if (Format != EValueJSONFormat::Persistence)
	{
		return Value;
	}
	return Wrap(Value);
}

TSharedPtr<FJsonValue> Wrap(const TSharedPtr<FJsonValue>& Value)
{
	TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
	Object->SetField(TEXT(""), Value);
	return MakeShared<FJsonValueObject>(::MoveTemp(Object));
}

TSharedPtr<FJsonValue> Unwrap(const TSharedPtr<FJsonValue>& Value, EValueJSONFormat Format)
{
	if (Format != EValueJSONFormat::Persistence)
	{
		return Value;
	}
	return Unwrap(*Value);
}

TSharedPtr<FJsonValue> Unwrap(const FJsonValue& Value)
{
	const TSharedPtr<FJsonObject>* JsonObject;
	if (!Value.TryGetObject(JsonObject))
	{
		return nullptr;
	}
	if (auto It = (*JsonObject)->Values.CreateConstIterator(); It)
	{
		return It.Value();
	}
	return nullptr;
}

#if WITH_VERSE_VM || defined(__INTELLISENSE__)
TSharedPtr<FJsonValue> VInt::ToJSON(FAllocationContext Context, EValueJSONFormat Format) const
{
	// TODO: Switch to using FNativeConverter when SOL-8589 is fixed and all JSON code runs in the open.
	if (IsInt64())
	{
		return Int64ToJson(AsInt64());
	}

	// While we still need the BPVM to load persistence data written by the new VM, runtime error if we're a BigInt.
	// TODO: Delete when new VM is on 100%
	if (Format == EValueJSONFormat::Persistence)
	{
		RAISE_VERSE_RUNTIME_ERROR_CODE(ERuntimeDiagnostic::ErrRuntime_IntegerBoundsExceeded);
		return nullptr;
	}

	FUtf8StringBuilderBase Builder;
	AppendDecimalToString(Builder, Context);
	return MakeShared<FJsonValueNumberString>(Builder.ToString());
}

TSharedPtr<FJsonValue> VCell::ToJSONImpl(FRunningContext Context, EValueJSONFormat Format, TMap<const void*, EVisitState>& VisitedObjects, const VerseVMToJsonCallback& Callback, uint32 RecursionDepth, FJsonObject* Defs)
{
	return nullptr;
}

TSharedPtr<FJsonValue> VRestValue::ToJSON(FRunningContext Context, EValueJSONFormat Format, TMap<const void*, EVisitState>& VisitedObjects, const VerseVMToJsonCallback& Callback, uint32 RecursionDepth, FJsonObject* Defs) const
{
	return Value.Get().ToJSON(Context, Format, VisitedObjects, Callback, RecursionDepth, Defs);
}

VValue VCell::FromJSONImpl(FRunningContext Context, const FJsonValue& JsonValue, EValueJSONFormat Format)
{
	UE_LOG(LogVerseVM, Warning, TEXT("Unsupported VCell type (%s) calling FromJSON"), *GetEmergentType()->CppClassInfo->DebugName());
	return VValue();
}

TSharedPtr<FJsonValue> ToJSON(FRunningContext Context, VValue Value, EValueJSONFormat Format, const VerseVMToJsonCallback& Callback, uint32 RecursionDepth, FJsonObject* Defs)
{
	TMap<const void*, EVisitState> VisitedObjects;
	return Value.ToJSON(Context, Format, VisitedObjects, Callback, RecursionDepth, Defs);
}

TSharedPtr<FJsonValue> VCell::ToJSON(FRunningContext Context, EValueJSONFormat Format, TMap<const void*, EVisitState>& VisitedObjects, const VerseVMToJsonCallback& Callback, uint32 RecursionDepth, FJsonObject* Defs)
{
	V_DIE_IF(IsA<VPlaceholder>());
	if (this == &GlobalFalse())
	{
		return MakeShared<FJsonValueBoolean>(false);
	}
	if (this == &GlobalTrue())
	{
		return MakeShared<FJsonValueBoolean>(true);
	}
	if (Format == EValueJSONFormat::Persistence && IsA<VType>() && !IsA<VOption>())
	{
		// check to make sure types don't get outputted to persistence which are the only non persistence cells also with 'ToJSON'
		return nullptr;
	}
	return GetEmergentType()->CppClassInfo->ToJSON(Context, this, Format, VisitedObjects, Callback, RecursionDepth, Defs);
}

VValue VCell::FromJSON(FRunningContext Context, const FJsonValue& JsonValue, EValueJSONFormat Format)
{
	return GetEmergentType()->CppClassInfo->FromJSON(Context, this, JsonValue, Format);
}

bool IsVisiting(VValue Value, TMap<const void*, ::Verse::EVisitState>& VisitedObjects)
{
	EVisitState* VisitState = nullptr;
	if (Value.IsCellOfType<VClass>() || Value.IsCellOfType<VObject>())
	{
		VisitState = VisitedObjects.Find(Value.ExtractCell());
	}
	if (Value.IsUObject())
	{
		VisitState = VisitedObjects.Find(Value.AsUObject());
	}
	return VisitState && *VisitState == EVisitState::Visiting;
}

TSharedPtr<FJsonValue> VValue::ToJSON(FRunningContext Context, EValueJSONFormat Format, TMap<const void*, EVisitState>& VisitedObjects, const VerseVMToJsonCallback& Callback, uint32 RecursionDepth, FJsonObject* Defs)
{
	if (Callback.IsSet())
	{
		if (TSharedPtr<FJsonValue> Result = Callback(Context, *this, Format, VisitedObjects, RecursionDepth, Defs))
		{
			return Result;
		}
	}

	if (IsVisiting(*this, VisitedObjects))
	{
		// We are in a cycle and must fail
		return nullptr;
	}

	if (IsInt())
	{
		return AsInt().ToJSON(Context, Format);
	}
	if (IsCell())
	{
		return AsCell().ToJSON(Context, Format, VisitedObjects, Callback, RecursionDepth, Defs);
	}
	if (VRef* Ref = ExtractTransparentRef())
	{
		return Ref->ToJSON(Context, Format, VisitedObjects, Callback, RecursionDepth, Defs);
	}
	if (IsUObject())
	{
		// UObject's should be handled by `Callback`, but we default to just name otherwise
		return MakeShared<FJsonValueString>(GetFullNameSafe(AsUObject()));
	}
	if (IsPlaceholder())
	{
		VPlaceholder& Placeholder = AsPlaceholder();
		VValue Root = Follow();
		if (Root.IsPlaceholder())
		{
			return nullptr;
		}
		return Root.ToJSON(Context, Format, VisitedObjects, Callback, RecursionDepth, Defs);
	}
	if (IsFloat())
	{
		return MakeShared<FJsonValueNumber>(AsFloat().AsDouble());
	}
	if (IsChar())
	{
		return Int64ToJson(AsChar());
	}
	if (IsChar32())
	{
		// Legacy representation
		return Int64ToJson(static_cast<int32>(AsChar32()));
	}
	return nullptr;
}

VValue VValue::FromJSON(FRunningContext Context, const FJsonValue& JsonValue, EValueJSONFormat Format)
{
	if (IsCell())
	{
		return AsCell().FromJSON(Context, JsonValue, Format);
	}
	return VValue();
}
#endif // WITH_VERSE_VM || defined(__INTELLISENSE__)

} // namespace Verse

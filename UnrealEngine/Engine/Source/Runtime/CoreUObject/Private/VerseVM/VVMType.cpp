// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_VERSE_VM || defined(__INTELLISENSE__)
#include "VerseVM/VVMType.h"
#include "VerseVM/Inline/VVMArrayBaseInline.h"
#include "VerseVM/Inline/VVMCellInline.h"
#include "VerseVM/Inline/VVMMapInline.h"
#include "VerseVM/VVMArray.h"
#include "VerseVM/VVMCppClassInfo.h"
#include "VerseVM/VVMEmergentType.h"
#include "VerseVM/VVMFalse.h"
#include "VerseVM/VVMJson.h"
#include "VerseVM/VVMMap.h"
#include "VerseVM/VVMOption.h"
#include "VerseVM/VVMStructuredArchiveVisitor.h"
#include "VerseVM/VVMValuePrinting.h"

namespace Verse
{

DEFINE_DERIVED_VCPPCLASSINFO(VType);
DEFINE_TRIVIAL_VISIT_REFERENCES(VType);

VType::VType(FAllocationContext Context, VEmergentType* Type)
	: VHeapValue(Context, Type)
{
}

DEFINE_DERIVED_VCPPCLASSINFO(VTrivialType)
DEFINE_TRIVIAL_VISIT_REFERENCES(VTrivialType);

TGlobalHeapPtr<VTrivialType> VTrivialType::Singleton;

void VTrivialType::Initialize(FAllocationContext Context)
{
	V_DIE_UNLESS(VEmergentTypeCreator::EmergentTypeForTrivialType);
	Singleton.Set(Context, new (Context.AllocateFastCell(sizeof(VTrivialType))) VTrivialType(Context));
}

VTrivialType::VTrivialType(FAllocationContext Context)
	: VType(Context, VEmergentTypeCreator::EmergentTypeForTrivialType.Get())
{
}

#define DEFAULT_JSON(Name)                                                                                                                                                                                                              \
	TSharedPtr<FJsonValue> V##Name##Type::ToJSONImpl(FRunningContext Context, EValueJSONFormat Format, TMap<const void*, EVisitState>& VisitedObjects, const VerseVMToJsonCallback& Callback, uint32 RecursionDepth, FJsonObject* Defs) \
	{                                                                                                                                                                                                                                   \
		if (Format == EValueJSONFormat::Persona)                                                                                                                                                                                        \
		{                                                                                                                                                                                                                               \
			return nullptr;                                                                                                                                                                                                             \
		}                                                                                                                                                                                                                               \
		TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();                                                                                                                                                                     \
		Object->SetStringField("Type", #Name);                                                                                                                                                                                          \
		return MakeShared<FJsonValueObject>(Object);                                                                                                                                                                                    \
	}                                                                                                                                                                                                                                   \
	VValue V##Name##Type::FromJSONImpl(FRunningContext Context, const FJsonValue& JsonValue, EValueJSONFormat Format)                                                                                                                   \
	{                                                                                                                                                                                                                                   \
		return VValue();                                                                                                                                                                                                                \
	}

#define DEFINE_PRIMITIVE_TYPE(Name)                                                                                           \
	DEFINE_DERIVED_VCPPCLASSINFO(V##Name##Type)                                                                               \
	DEFINE_TRIVIAL_VISIT_REFERENCES(V##Name##Type)                                                                            \
	TGlobalTrivialEmergentTypePtr<&V##Name##Type::StaticCppClassInfo> V##Name##Type::GlobalTrivialEmergentType;               \
	TGlobalHeapPtr<V##Name##Type> V##Name##Type::Singleton;                                                                   \
	void V##Name##Type::SerializeLayout(FAllocationContext Context, V##Name##Type*& This, FStructuredArchiveVisitor& Visitor) \
	{                                                                                                                         \
		if (Visitor.IsLoading())                                                                                              \
		{                                                                                                                     \
			This = Singleton.Get();                                                                                           \
		}                                                                                                                     \
	}                                                                                                                         \
	void V##Name##Type::SerializeImpl(FAllocationContext Context, FStructuredArchiveVisitor& Visitor)                         \
	{                                                                                                                         \
	}                                                                                                                         \
	void V##Name##Type::Initialize(FAllocationContext Context)                                                                \
	{                                                                                                                         \
		Singleton.Set(Context, new (Context.AllocateFastCell(sizeof(V##Name##Type))) V##Name##Type(Context));                 \
	}                                                                                                                         \
	V##Name##Type::V##Name##Type(FAllocationContext Context)                                                                  \
		: VType(Context, &GlobalTrivialEmergentType.Get(Context))                                                             \
	{                                                                                                                         \
	}

#define DEFINE_PRIMITIVE_TYPE_DEFAULT_JSON(Name) \
	DEFINE_PRIMITIVE_TYPE(Name)                  \
	DEFAULT_JSON(Name)

DEFINE_PRIMITIVE_TYPE_DEFAULT_JSON(Any)
DEFINE_PRIMITIVE_TYPE_DEFAULT_JSON(Comparable)
DEFINE_PRIMITIVE_TYPE_DEFAULT_JSON(Function)
DEFINE_PRIMITIVE_TYPE_DEFAULT_JSON(Range)
DEFINE_PRIMITIVE_TYPE_DEFAULT_JSON(Rational) // TODO: Should we output Persona support rationals as Number?
DEFINE_PRIMITIVE_TYPE_DEFAULT_JSON(Reference)
DEFINE_PRIMITIVE_TYPE_DEFAULT_JSON(Persistable)
DEFINE_PRIMITIVE_TYPE_DEFAULT_JSON(Void)

DEFINE_PRIMITIVE_TYPE(Char8)
TSharedPtr<FJsonValue> VChar8Type::ToJSONImpl(FRunningContext Context, EValueJSONFormat Format, TMap<const void*, EVisitState>& VisitedObjects, const VerseVMToJsonCallback& Callback, uint32 RecursionDepth, FJsonObject* Defs)
{
	TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
	Object->SetStringField(PERSONA_FIELD(Type), Persona::IntegerString);
	Object->SetNumberField(PERSONA_FIELD(Minimum), 0);
	Object->SetNumberField(PERSONA_FIELD(Maximum), UINT8_MAX);
	return MakeShared<FJsonValueObject>(Object);
}

VValue VChar8Type::FromJSONImpl(FRunningContext Context, const FJsonValue& JsonValue, EValueJSONFormat Format)
{
	uint8 Char8Value;
	if (!JsonValue.TryGetNumber(Char8Value))
	{
		return VValue();
	}
	return VValue::Char(Char8Value);
}

DEFINE_PRIMITIVE_TYPE(Char32)
TSharedPtr<FJsonValue> VChar32Type::ToJSONImpl(FRunningContext Context, EValueJSONFormat Format, TMap<const void*, EVisitState>& VisitedObjects, const VerseVMToJsonCallback& Callback, uint32 RecursionDepth, FJsonObject* Defs)
{
	if (Format == EValueJSONFormat::Persona)
	{
		return nullptr;
	}
	TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
	Object->SetStringField("Type", "Char32");
	return MakeShared<FJsonValueObject>(Object);
}

VValue VChar32Type::FromJSONImpl(FRunningContext Context, const FJsonValue& JsonValue, EValueJSONFormat Format)
{
	uint32 Int32Value;
	if (!JsonValue.TryGetNumber(Int32Value))
	{
		return VValue();
	}
	return VValue::Char32(Int32Value);
}

DEFINE_PRIMITIVE_TYPE(Logic)
TSharedPtr<FJsonValue> VLogicType::ToJSONImpl(FRunningContext Context, EValueJSONFormat Format, TMap<const void*, EVisitState>& VisitedObjects, const VerseVMToJsonCallback& Callback, uint32 RecursionDepth, FJsonObject* Defs)
{
	TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
	Object->SetStringField(PERSONA_FIELD(Type), Persona::BooleanString);
	return MakeShared<FJsonValueObject>(Object);
}

VValue VLogicType::FromJSONImpl(FRunningContext Context, const FJsonValue& JsonValue, EValueJSONFormat Format)
{
	bool BoolValue;
	if (!JsonValue.TryGetBool(BoolValue))
	{
		return VValue();
	}
	return BoolValue ? VValue(GlobalTrue()) : VValue(GlobalFalse());
}

#define DEFINE_STRUCTURAL_TYPE(Name, Fields)                                                                                  \
	DEFINE_DERIVED_VCPPCLASSINFO(V##Name##Type)                                                                               \
	TGlobalTrivialEmergentTypePtr<&V##Name##Type::StaticCppClassInfo> V##Name##Type::GlobalTrivialEmergentType;               \
	template <typename TVisitor>                                                                                              \
	void V##Name##Type::VisitReferencesImpl(TVisitor& Visitor)                                                                \
	{                                                                                                                         \
		Fields(VISIT_STRUCTURAL_TYPE_FIELD);                                                                                  \
	}                                                                                                                         \
	void V##Name##Type::SerializeLayout(FAllocationContext Context, V##Name##Type*& This, FStructuredArchiveVisitor& Visitor) \
	{                                                                                                                         \
		if (Visitor.IsLoading())                                                                                              \
		{                                                                                                                     \
			This = &V##Name##Type::New(Context, Fields(INIT_STRUCTURAL_TYPE_ARG));                                            \
		}                                                                                                                     \
	}                                                                                                                         \
	void V##Name##Type::SerializeImpl(FAllocationContext Context, FStructuredArchiveVisitor& Visitor)                         \
	{                                                                                                                         \
		Fields(VISIT_STRUCTURAL_TYPE_FIELD);                                                                                  \
	}

#define DEFINE_STRUCTURAL_TYPE_DEFAULT_JSON(Name, Fields) \
	DEFINE_STRUCTURAL_TYPE(Name, Fields)                  \
	DEFAULT_JSON(Name)

#define VISIT_STRUCTURAL_TYPE_FIELD(Name) Visitor.Visit(Name, TEXT(#Name))
#define INIT_STRUCTURAL_TYPE_ARG(Name) VValue()

#define TYPE_FIELDS(Field) Field(PositiveType)
DEFINE_STRUCTURAL_TYPE_DEFAULT_JSON(Type, TYPE_FIELDS)
#undef TYPE_FIELDS

#define TYPE_FIELDS(Field) Field(ElementType)
DEFINE_STRUCTURAL_TYPE_DEFAULT_JSON(Generator, TYPE_FIELDS)
#undef TYPE_FIELDS

#define TYPE_FIELDS(Field) Field(ValueType)
DEFINE_STRUCTURAL_TYPE_DEFAULT_JSON(Pointer, TYPE_FIELDS)
#undef TYPE_FIELDS

#define TYPE_FIELDS(Field) Field(SuperType)
DEFINE_STRUCTURAL_TYPE_DEFAULT_JSON(Concrete, TYPE_FIELDS)
#undef TYPE_FIELDS

#define TYPE_FIELDS(Field) Field(SuperType)
DEFINE_STRUCTURAL_TYPE_DEFAULT_JSON(Castable, TYPE_FIELDS)
#undef TYPE_FIELDS

#define TYPE_FIELDS(Field) Field(ElementType)
DEFINE_STRUCTURAL_TYPE(Array, TYPE_FIELDS)
#undef TYPE_FIELDS
TSharedPtr<FJsonValue> VArrayType::ToJSONImpl(FRunningContext Context, EValueJSONFormat Format, TMap<const void*, EVisitState>& VisitedObjects, const VerseVMToJsonCallback& Callback, uint32 RecursionDepth, FJsonObject* Defs)
{
	TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
	if (ElementType.Get().DynamicCast<VChar8Type>())
	{
		Object->SetStringField(PERSONA_FIELD(Type), Persona::StringString);
		return MakeShared<FJsonValueObject>(Object);
	}

	TSharedPtr<FJsonValue> Items = ElementType.Get().ToJSON(Context, Format, VisitedObjects, Callback, RecursionDepth, Defs);
	if (!Items)
	{
		return nullptr;
	}
	Object->SetStringField(PERSONA_FIELD(Type), Persona::ArrayString);
	Object->SetField(PERSONA_FIELD(Items), ::MoveTemp(Items));
	return MakeShared<FJsonValueObject>(Object);
}

static VArray* ArrayFromJson(
	FRunningContext Context,
	const FJsonObject& JsonObject,
	VArrayType& ArrayType,
	EValueJSONFormat Format)
{
	VType* ElementType = ArrayType.ElementType.Get().Follow().DynamicCast<VType>();
	if (!ElementType)
	{
		return nullptr;
	}
	auto&& JsonValues = JsonObject.Values;
	auto NumElements = JsonValues.Num();
	VArray& Result = VArray::New(Context, NumElements, EArrayType::VValue);
	VValue* ElementValues = Result.GetData<VValue>();
	for (auto&& [FieldKey, FieldJsonValue] : JsonValues)
	{
		// A dummy padding field implies an empty tuple.
		int32 Index;
		if (FieldKey.FindChar('$', Index))
		{
			V_DIE_UNLESS(NumElements == 1);
			return &VArray::New(Context, 0, EArrayType::None);
		}
		VValue Value = ElementType->FromJSON(Context, *FieldJsonValue, Format);
		if (!Value)
		{
			return nullptr;
		}
		*ElementValues = Value;
		++ElementValues;
	}
	return &Result;
}

static VArray* ArrayFromJson(
	FRunningContext Context,
	const TArray<TSharedPtr<FJsonValue>>& JsonArray,
	VArrayType& ArrayType,
	EValueJSONFormat Format)
{
	VType* ElementType = ArrayType.ElementType.Get().Follow().DynamicCast<VType>();
	if (!ElementType)
	{
		return nullptr;
	}
	auto NumElements = JsonArray.Num();
	VArray& Result = VArray::New(Context, NumElements, EArrayType::VValue);
	VValue* ElementValues = Result.GetData<VValue>();
	for (auto&& ElementJsonValue : JsonArray)
	{
		VValue Value = ElementType->FromJSON(Context, *ElementJsonValue, Format);
		if (!Value)
		{
			return nullptr;
		}
		*ElementValues = Value;
		++ElementValues;
	}
	return &Result;
}

static VArray* ArrayFromJson(
	FRunningContext Context,
	const FJsonValue& JsonValue,
	VArrayType& ArrayType,
	EValueJSONFormat Format)
{
	FUtf8String String;
	if (JsonValue.TryGetUtf8String(String))
	{
		return &VArray::New(Context, String);
	}
	const TSharedPtr<FJsonObject>* JsonObject;
	if (JsonValue.TryGetObject(JsonObject))
	{
		return ArrayFromJson(Context, **JsonObject, ArrayType, Format);
	}
	const TArray<TSharedPtr<FJsonValue>>* JsonArray;
	if (JsonValue.TryGetArray(JsonArray))
	{
		return ArrayFromJson(Context, *JsonArray, ArrayType, Format);
	}
	return nullptr;
}

VValue VArrayType::FromJSONImpl(FRunningContext Context, const FJsonValue& JsonValue, EValueJSONFormat Format)
{
	VArray* Array = ArrayFromJson(Context, JsonValue, *this, Format);
	if (!Array)
	{
		return VValue();
	}
	return *Array;
}

#define TYPE_FIELDS(Field) Field(KeyType), Field(ValueType)
DEFINE_STRUCTURAL_TYPE(WeakMap, TYPE_FIELDS)
#undef TYPE_FIELDS
TSharedPtr<FJsonValue> VWeakMapType::ToJSONImpl(FRunningContext Context, EValueJSONFormat Format, TMap<const void*, EVisitState>& VisitedObjects, const VerseVMToJsonCallback& Callback, uint32 RecursionDepth, FJsonObject* Defs)
{
	return nullptr; // TODO?
}
VValue VWeakMapType::FromJSONImpl(FRunningContext Context, const FJsonValue& JsonValue, EValueJSONFormat Format)
{
	return VValue(); // TODO?
}

DEFINE_DERIVED_VCPPCLASSINFO(VMapType)
DEFINE_TRIVIAL_VISIT_REFERENCES(VMapType)
TGlobalTrivialEmergentTypePtr<&VMapType::StaticCppClassInfo> VMapType::GlobalTrivialEmergentType;

void VMapType::SerializeLayout(FAllocationContext Context, VMapType*& This, FStructuredArchiveVisitor& Visitor)
{
	if (Visitor.IsLoading())
	{
		This = &VMapType::New(Context, VValue(), VValue());
	}
}

void VMapType::SerializeImpl(FAllocationContext Context, FStructuredArchiveVisitor& Visitor)
{
	VWeakMapType::SerializeImpl(Context, Visitor);
}

TSharedPtr<FJsonValue> VMapType::ToJSONImpl(FRunningContext Context, EValueJSONFormat Format, TMap<const void*, EVisitState>& VisitedObjects, const VerseVMToJsonCallback& Callback, uint32 RecursionDepth, FJsonObject* Defs)
{
	TSharedPtr<FJsonValue> Key = KeyType.Get().ToJSON(Context, Format, VisitedObjects, Callback, RecursionDepth, Defs);
	if (!Key)
	{
		return nullptr;
	}
	TSharedPtr<FJsonValue> Value = ValueType.Get().ToJSON(Context, Format, VisitedObjects, Callback, RecursionDepth, Defs);
	if (!Value)
	{
		return nullptr;
	}
	TSharedRef<FJsonObject> Properties = MakeShared<FJsonObject>();
	Properties->SetField(PERSONA_FIELD(Key), ::MoveTemp(Key));
	Properties->SetField(PERSONA_FIELD(Value), ::MoveTemp(Value));
	TSharedRef<FJsonObject> Items = MakeShared<FJsonObject>();
	Items->SetStringField(PERSONA_FIELD(Type), Persona::ObjectString);
	Items->SetObjectField(PERSONA_FIELD(Properties), ::MoveTemp(Properties));
	Items->SetArrayField(PERSONA_FIELD(Required), {MakeShared<FJsonValueString>(Persona::KeyString), MakeShared<FJsonValueString>(Persona::ValueString)});
	TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
	Object->SetStringField(PERSONA_FIELD(Type), Persona::ArrayString);
	Object->SetObjectField(PERSONA_FIELD(Items), ::MoveTemp(Items));
	return MakeShared<FJsonValueObject>(Object);
}

VValue VMapType::FromJSONImpl(FRunningContext Context, const FJsonValue& JsonValue, EValueJSONFormat Format)
{
	const TArray<TSharedPtr<FJsonValue>>* JsonArray;
	if (!JsonValue.TryGetArray(JsonArray))
	{
		return VValue();
	}
	VType* Key = KeyType.Get().Follow().DynamicCast<VType>();
	if (!Key)
	{
		return VValue();
	}
	VType* Value = ValueType.Get().Follow().DynamicCast<VType>();
	if (!Value)
	{
		return VValue();
	}
	VMap& Result = VMapBase::New<VMap>(Context, JsonArray->Num());
	FString KeyString = Format == EValueJSONFormat::Persistence ? Persistence::KeyKey : Persona::KeyString;
	FString ValueString = Format == EValueJSONFormat::Persistence ? Persistence::ValueKey : Persona::ValueString;
	for (const TSharedPtr<FJsonValue>& ElementJsonValue : *JsonArray)
	{
		const TSharedPtr<FJsonObject>* JsonObject;
		if (!ElementJsonValue->TryGetObject(JsonObject))
		{
			return VValue();
		}
		TSharedPtr<FJsonValue> KeyJsonValue = (*JsonObject)->TryGetField(KeyString);
		if (!KeyJsonValue)
		{
			return VValue();
		}
		// Unwrap from object to match legacy representation.
		KeyJsonValue = Unwrap(KeyJsonValue, Format);
		if (!KeyJsonValue)
		{
			return VValue();
		}
		TSharedPtr<FJsonValue> ValueJsonValue = (*JsonObject)->TryGetField(ValueString);
		if (!ValueJsonValue)
		{
			return VValue();
		}
		// Unwrap from object to match legacy representation.
		ValueJsonValue = Unwrap(ValueJsonValue, Format);
		if (!ValueJsonValue)
		{
			return VValue();
		}
		VValue KeyVal = Key->FromJSON(Context, *KeyJsonValue, Format);
		if (!KeyVal)
		{
			return VValue();
		}
		VValue ValueVal = Value->FromJSON(Context, *ValueJsonValue, Format);
		if (!ValueVal)
		{
			return VValue();
		}
		Result.Add(Context, KeyVal, ValueVal);
	}
	return Result;
}

#define TYPE_FIELDS(Field) Field(ValueType)
DEFINE_STRUCTURAL_TYPE(Option, TYPE_FIELDS)
#undef TYPE_FIELDS
TSharedPtr<FJsonValue> VOptionType::ToJSONImpl(FRunningContext Context, EValueJSONFormat Format, TMap<const void*, EVisitState>& VisitedObjects, const VerseVMToJsonCallback& Callback, uint32 RecursionDepth, FJsonObject* Defs)
{
	TSharedPtr<FJsonValue> Value = ValueType.Get().ToJSON(Context, Format, VisitedObjects, Callback, RecursionDepth, Defs);
	if (!Value)
	{
		return nullptr;
	}
	TArray<TSharedPtr<FJsonValue>> Options;
	Options.Reserve(2);
	TSharedRef<FJsonObject> False = MakeShared<FJsonObject>();
	False->SetStringField(PERSONA_FIELD(Type), Persona::BooleanString);
	Options.Emplace(MakeShared<FJsonValueObject>(::MoveTemp(False)));

	TSharedRef<FJsonObject> Truth = MakeShared<FJsonObject>();
	Truth->SetStringField(PERSONA_FIELD(Type), Persona::ObjectString);
	Truth->SetField(PERSONA_FIELD(Properties), Wrap(Value));
	Options.Emplace(MakeShared<FJsonValueObject>(::MoveTemp(Truth)));

	TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
	Object->SetArrayField(PERSONA_FIELD(AnyOf), Options);
	return MakeShared<FJsonValueObject>(Object);
}

VValue VOptionType::FromJSONImpl(FRunningContext Context, const FJsonValue& JsonValue, EValueJSONFormat Format)
{
	bool BoolValue;
	if (JsonValue.TryGetBool(BoolValue))
	{
		if (BoolValue)
		{
			return VValue();
		}
		return GlobalFalse();
	}
	TSharedPtr<FJsonValue> ValueJSON = Unwrap(JsonValue);
	if (!ValueJSON)
	{
		return VValue();
	}
	VValue Value = ValueType.Get().FromJSON(Context, *ValueJSON, Format);
	if (!Value)
	{
		return VValue();
	}
	return VOption::New(Context, Value);
}

#undef DEFAULT_JSON
#undef DEFINE_PRIMITIVE_TYPE
#undef DEFINE_PRIMITIVE_TYPE_DEFAULT_JSON
#undef DEFINE_STRUCTURAL_TYPE
#undef DEFINE_STRUCTURAL_TYPE_DEFAULT_JSON
#undef VISIT_STRUCTURAL_TYPE_FIELD
#undef INIT_STRUCTURAL_TYPE_ARG

} // namespace Verse
#endif // WITH_VERSE_VM || defined(__INTELLISENSE__)

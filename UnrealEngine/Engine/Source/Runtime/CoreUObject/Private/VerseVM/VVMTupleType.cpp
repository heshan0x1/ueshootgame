// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_VERSE_VM || defined(__INTELLISENSE__)

#include "VerseVM/VVMTupleType.h"
#include "VerseVM/Inline/VVMMarkStackVisitorInline.h"
#include "VerseVM/VVMEngineEnvironment.h"
#include "VerseVM/VVMGlobalProgram.h"
#include "VerseVM/VVMJson.h"
#include "VerseVM/VVMPackage.h"
#include "VerseVM/VVMPackageName.h"
#include "VerseVM/VVMVerse.h"

namespace Verse
{

DEFINE_DERIVED_VCPPCLASSINFO(VTupleType);
TGlobalTrivialEmergentTypePtr<&VTupleType::StaticCppClassInfo> VTupleType::GlobalTrivialEmergentType;

template <typename TVisitor>
void VTupleType::VisitReferencesImpl(TVisitor& Visitor)
{
	Visitor.Visit(MangledName, TEXT("MangledName"));
	Visitor.Visit(Elements, NumElements, TEXT("Elements"));

	FCellUniqueLock Lock(Mutex);
	Visitor.Visit(AssociatedUStructs, TEXT("AssociatedUStructs"));
}

void VTupleType::SerializeLayout(FAllocationContext Context, VTupleType*& This, FStructuredArchiveVisitor& Visitor)
{
	FUtf8String MangledNameString;
	int32 NumElements = 0;
	if (!Visitor.IsLoading())
	{
		MangledNameString = This->MangledName->AsStringView();
		NumElements = This->NumElements;
	}

	Visitor.Visit(MangledNameString, TEXT("MangledName"));
	Visitor.Visit(NumElements, TEXT("NumElements"));
	if (Visitor.IsLoading())
	{
		VUniqueString& MangledName = VUniqueString::New(Context, MangledNameString);
		bool bNew = false;
		This = &VTupleType::New(Context, *GlobalProgram, MangledName, NumElements, bNew);
	}
}

void VTupleType::SerializeImpl(FAllocationContext Context, FStructuredArchiveVisitor& Visitor)
{
	// This will run regardless of whether this is the first package to deserialize this tuple,
	// but that's okay because they're the same element types for all packages.
	Visitor.Visit(Elements, NumElements, TEXT("Elements"));
}

VTupleType& VTupleType::New(FAllocationContext Context, VProgram& Program, VUniqueString& InMangledName, int32 InNumElements, bool& bOutNew)
{
	if (VTupleType* TupleTypeEntry = Program.LookupTupleType(Context, InMangledName))
	{
		bOutNew = false;
		return *TupleTypeEntry;
	}
	bOutNew = true;

	size_t NumBytes = offsetof(VTupleType, Elements) + InNumElements * sizeof(Elements[0]);
	VTupleType* TupleType = new (Context.Allocate(FHeap::DestructorSpace, NumBytes)) VTupleType(Context, InMangledName, InNumElements);
	Program.AddTupleType(Context, InMangledName, *TupleType);
	return *TupleType;
}

void VTupleType::AddUStruct(FAllocationContext Context, UPackage* Package, UVerseStruct* Struct)
{
	FCellUniqueLock Lock(Mutex);
	AssociatedUStructs.Add({Context, Package}, {Context, Struct});
}

UVerseStruct* VTupleType::CreateUStruct(FAllocationContext Context, UPackage* Package, bool bIsInstanced)
{
	IEngineEnvironment* Environment = VerseVM::GetEngineEnvironment();
	check(Environment);

	// Create the UE struct
	UVerseStruct* UeStruct = NewObject<UVerseStruct>(Package, FName(MangledName->AsStringView()), RF_Public /* | RF_Transient*/);
	AddUStruct(Context, Package, UeStruct);

	UeStruct->VerseClassFlags |= VCLASS_Tuple;

#if WITH_EDITOR
	UeStruct->SetMetaData(TEXT("IsBlueprintBase"), TEXT("false"));
#endif

	// Generate properties
	FField::FLinkedListBuilder PropertyListBuilder(&UeStruct->ChildProperties);
	for (int32 Index = 0; Index < NumElements; ++Index)
	{
		VValue TypeValue = Elements[Index].Follow();
		V_DIE_IF(TypeValue.IsPlaceholder());

		VType& ElementType = TypeValue.StaticCast<VType>();
		FProperty* FieldProperty = Environment->CreateProperty(Context, Package, UeStruct, FUtf8String::Printf("Elem%d", Index), FUtf8String::Printf("Elem%d", Index), ElementType, true, bIsInstanced);
		PropertyListBuilder.AppendNoTerminate(*FieldProperty);
	}

	// Finalize struct
	UeStruct->Bind();
	UeStruct->StaticLink(/*bRelinkExistingProperties =*/true);

	return UeStruct;
}

TSharedPtr<FJsonValue> VTupleType::ToJSONImpl(FRunningContext Context, EValueJSONFormat Format, TMap<const void*, EVisitState>& VisitedObjects, const VerseVMToJsonCallback& Callback, uint32 RecursionDepth, FJsonObject* Defs)
{
	if (EVisitState* VisitState = VisitedObjects.Find(this); VisitState && *VisitState == EVisitState::Visiting)
	{
		// We are in a cycle and must fail
		return nullptr;
	}
	VisitedObjects.Add(this, Verse::EVisitState::Visiting);

	TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
	Object->SetStringField(PERSONA_FIELD(Type), Persona::ObjectString);

	TSharedRef<FJsonObject> Properties = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> RequiredProperties;
	for (int32 Index = 0; Index < NumElements; ++Index)
	{
		VValue TypeValue = Elements[Index].Follow();
		V_DIE_IF(TypeValue.IsPlaceholder());

		TSharedPtr<FJsonValue> Value = TypeValue.ToJSON(Context, Format, VisitedObjects, Callback, RecursionDepth, Defs);
		if (!Value)
		{
			return nullptr;
		}
		Properties->SetField(FString::FromInt(Index), ::MoveTemp(Value));
		RequiredProperties.Add(MakeShared<FJsonValueString>(FString::FromInt(Index)));
	}
	Object->SetObjectField(PERSONA_FIELD(Properties), ::MoveTemp(Properties));
	Object->SetArrayField(PERSONA_FIELD(Required), ::MoveTemp(RequiredProperties));

	VisitedObjects.Add(this, EVisitState::Visited);
	return MakeShared<FJsonValueObject>(Object);
}

static VArray* TupleFromJSON(
	FRunningContext Context,
	const FJsonObject& JsonObject,
	VTupleType& TupleType,
	EValueJSONFormat Format)
{
	TArrayView<TWriteBarrier<VValue>> Elements = TupleType.GetElements();
	if (Elements.Num() == 0)
	{
		// Ignore object containing only a padding field.
		return &VArray::New(Context, 0, EArrayType::None);
	}
	VArray& Result = VArray::New(Context, Elements.Num(), EArrayType::VValue);
	auto&& JsonValues = JsonObject.Values;
	if (Elements.Num() != JsonValues.Num())
	{
		return nullptr;
	}
	auto ElementTypes = Elements.GetData();
	VValue* ElementValues = Result.GetData<VValue>();
	for (auto&& [FieldKey, FieldJsonValue] : JsonValues)
	{
		VType* ElementType = ElementTypes->Get().Follow().DynamicCast<VType>();
		if (!ElementType)
		{
			return nullptr;
		}
		VValue Value = ElementType->FromJSON(Context, *FieldJsonValue, Format);
		if (!Value)
		{
			return nullptr;
		}
		*ElementValues = Value;
		++ElementTypes;
		++ElementValues;
	}
	return &Result;
}

static VArray* TupleFromJSON(
	FRunningContext Context,
	const TArray<TSharedPtr<FJsonValue>>& JsonArray,
	VTupleType& TupleType,
	EValueJSONFormat Format)
{
	TArrayView<TWriteBarrier<VValue>> Elements = TupleType.GetElements();
	if (Elements.Num() != JsonArray.Num())
	{
		return nullptr;
	}
	VArray& Result = VArray::New(Context, Elements.Num(), EArrayType::VValue);
	auto ElementTypes = Elements.GetData();
	VValue* ElementValues = Result.GetData<VValue>();
	for (const TSharedPtr<FJsonValue>& ElementJsonValue : JsonArray)
	{
		VType* ElementType = ElementTypes->Get().Follow().DynamicCast<VType>();
		if (!ElementType)
		{
			return nullptr;
		}
		VValue Value = ElementType->FromJSON(Context, *ElementJsonValue, Format);
		if (!Value)
		{
			return nullptr;
		}
		*ElementValues = Value;
		++ElementTypes;
		++ElementValues;
	}
	return &Result;
}

static VArray* TupleFromJSON(
	FRunningContext Context,
	const FJsonValue& JsonValue,
	VTupleType& TupleType,
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
		return TupleFromJSON(Context, **JsonObject, TupleType, Format);
	}
	const TArray<TSharedPtr<FJsonValue>>* JsonArray;
	if (JsonValue.TryGetArray(JsonArray))
	{
		return TupleFromJSON(Context, *JsonArray, TupleType, Format);
	}
	return nullptr;
}

VValue VTupleType::FromJSONImpl(FRunningContext Context, const FJsonValue& JsonValue, EValueJSONFormat Format)
{
	VArray* Array = TupleFromJSON(Context, JsonValue, *this, Format);
	if (!Array)
	{
		return VValue();
	}
	return *Array;
}

} // namespace Verse
#endif // WITH_VERSE_VM || defined(__INTELLISENSE__)

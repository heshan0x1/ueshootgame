// Copyright Epic Games, Inc. All Rights Reserved.

#if !WITH_VERSE_BPVM || defined(__INTELLISENSE__)
#include "VerseVM/VVMNativeRef.h"
#include "UObject/EnumProperty.h"
#include "UObject/PropertyOptional.h"
#include "UObject/VerseClassProperty.h"
#include "UObject/VerseStringProperty.h"
#include "UObject/VerseValueProperty.h"
#include "VerseVM/Inline/VVMCellInline.h"
#include "VerseVM/Inline/VVMEnumerationInline.h"
#include "VerseVM/Inline/VVMMarkStackVisitorInline.h"
#include "VerseVM/Inline/VVMObjectInline.h"
#include "VerseVM/Inline/VVMRefInline.h"
#include "VerseVM/Inline/VVMShapeInline.h"
#include "VerseVM/VBPVMRuntimeType.h"
#include "VerseVM/VVMNativeConverter.h"
#include "VerseVM/VVMNativeRational.h"
#include "VerseVM/VVMRuntimeError.h"
#include "VerseVM/VVMValueObject.h"
#include "VerseVM/VVMVerseEnum.h"
#include "VerseVM/VVMVerseStruct.h"

namespace Verse
{

DEFINE_DERIVED_VCPPCLASSINFO(VNativeRef);
TGlobalTrivialEmergentTypePtr<&VNativeRef::StaticCppClassInfo> VNativeRef::GlobalTrivialEmergentType;

FOpResult VNativeRef::Get(FAllocationContext Context)
{
	V_DIE_UNLESS(Type == EType::FProperty);

	if (UObject* Object = Base.Get().ExtractUObject())
	{
		return Get(Context, Object, UProperty);
	}
	else if (VNativeStruct* Struct = Base.Get().DynamicCast<VNativeStruct>())
	{
		return Get(Context, Struct->GetStruct(), UProperty);
	}
	else
	{
		VERSE_UNREACHABLE();
	}
}

FOpResult VNativeRef::Get(FAllocationContext Context, const void* Container, FProperty* Property)
{
	AutoRTFM::UnreachableIfClosed("#jira SOL-8415");

	if (FVRestValueProperty* ValueProperty = CastField<FVRestValueProperty>(Property))
	{
		// Native field with VRestValue type: any, comparable, persistable, type, function
		VRestValue* NativeValue = ValueProperty->ContainerPtrToValuePtr<VRestValue>(const_cast<void*>(Container));
		V_RETURN(NativeValue->Get(Context));
	}
	else if (FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
	{
		UEnum* UeEnum = EnumProperty->GetEnum();
		if (UeEnum == StaticEnum<EVerseTrue>())
		{
			// Get value of EVerseTrue even though technically not necessary as it's always zero
			const EVerseTrue* NativeValue = EnumProperty->ContainerPtrToValuePtr<EVerseTrue>(Container);
			V_RETURN(FNativeConverter::ToVValue(Context, *NativeValue));
		}

		// Convert integer value to corresponding VEnumerator cell
		UVerseEnum* VerseEnum = CastChecked<UVerseEnum>(UeEnum);
		VEnumeration* Enumeration = VerseEnum->Enumeration.Get();
		V_DIE_UNLESS(EnumProperty->GetUnderlyingProperty()->IsA<FByteProperty>());
		const uint8* NativeValue = EnumProperty->ContainerPtrToValuePtr<uint8>(Container);
		V_RETURN(Enumeration->GetEnumeratorChecked(*NativeValue));
	}
	else if (FBoolProperty* LogicProperty = CastField<FBoolProperty>(Property))
	{
		const bool* NativeValue = LogicProperty->ContainerPtrToValuePtr<bool>(Container);
		V_RETURN(FNativeConverter::ToVValue(Context, *NativeValue));
	}
	else if (FInt64Property* IntProperty = CastField<FInt64Property>(Property))
	{
		const int64* NativeValue = IntProperty->ContainerPtrToValuePtr<int64>(Container);
		V_RETURN(FNativeConverter::ToVValue(Context, *NativeValue));
	}
	else if (FDoubleProperty* FloatProperty = CastField<FDoubleProperty>(Property))
	{
		const double* NativeValue = FloatProperty->ContainerPtrToValuePtr<double>(Container);
		V_RETURN(FNativeConverter::ToVValue(Context, *NativeValue));
	}
	else if (FByteProperty* CharProperty = CastField<FByteProperty>(Property))
	{
		const UTF8CHAR* NativeValue = CharProperty->ContainerPtrToValuePtr<UTF8CHAR>(Container);
		V_RETURN(FNativeConverter::ToVValue(Context, *NativeValue));
	}
	else if (FIntProperty* Char32Property = CastField<FIntProperty>(Property))
	{
		const UTF32CHAR* NativeValue = Char32Property->ContainerPtrToValuePtr<UTF32CHAR>(Container);
		V_RETURN(FNativeConverter::ToVValue(Context, *NativeValue));
	}
	else if (FClassProperty* TypeProperty = CastField<FClassProperty>(Property))
	{
		// VerseVM does not use FClassProperty or FVerseClassProperty- fall through to legacy conversion.
	}
	else if (FObjectProperty* ObjectProperty = CastField<FObjectProperty>(Property))
	{
		if (ObjectProperty->PropertyClass == UStruct::StaticClass())
		{
			// VerseVM does not use FObjectProperty for types- fall through to legacy conversion.
		}
		else
		{
			if (UObject* const* NativeValue = ObjectProperty->ContainerPtrToValuePtr<UObject*>(Container); *NativeValue)
			{
				V_RETURN(FNativeConverter::ToVValue(Context, *NativeValue));
			}
			Context.RaiseVerseRuntimeError(ERuntimeDiagnostic::ErrRuntime_NativeInternal, FText::FromString("Null UObject encountered."));
			return {FOpResult::Error};
		}
	}
	else if (FStructProperty* StructProperty = CastField<FStructProperty>(Property))
	{
		if (StructProperty->Struct == FVerseRational::StaticStruct())
		{
			const FVerseRational* NativeValue = StructProperty->ContainerPtrToValuePtr<FVerseRational>(Container);
			V_RETURN(FNativeConverter::ToVValue(Context, *NativeValue));
		}

		const void* NativeValue = StructProperty->ContainerPtrToValuePtr<void>(Container);
		FOpResult Result = GetStruct(Context, NativeValue, StructProperty->Struct);
		if (Result.IsReturn() || Result.IsError())
		{
			return Result;
		}
		V_DIE_UNLESS(Result.Kind == FOpResult::Fail);
	}
	else if (FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
	{
		FScriptArrayHelper_InContainer NativeValue(ArrayProperty, Container);
		int NumElements = NativeValue.Num();
		VArray& Array = VArray::New(Context, NumElements, [Context, ArrayProperty, &NativeValue](uint32 Index) {
			FOpResult ArrayElem = VNativeRef::Get(Context, NativeValue.GetElementPtr(Index), ArrayProperty->Inner);
			if (!ArrayElem.IsReturn())
			{
				// When a runtime error occurs, we can immediately halt VArray construction by returning an
				// uninitialized VValue.
				V_DIE_UNLESS(ArrayElem.IsError());
				return VValue();
			}
			return ArrayElem.Value;
		});
		if (NumElements > 0 && Array.GetArrayType() == EArrayType::None)
		{
			// If the array should have elements but has no backing store, a runtime error occurred mid-construction.
			return {FOpResult::Error};
		}
		V_RETURN(Array);
	}
	else if (FVerseStringProperty* StringProperty = CastField<FVerseStringProperty>(Property))
	{
		const FNativeString* NativeValue = StringProperty->ContainerPtrToValuePtr<FNativeString>(Container);
		V_RETURN(FNativeConverter::ToVValue(Context, *NativeValue));
	}
	else if (FMapProperty* MapProperty = CastField<FMapProperty>(Property))
	{
		FScriptMapHelper_InContainer NativeValue(MapProperty, Container);

		TArray<TPair<VValue, VValue>> Pairs;
		Pairs.Reserve(NativeValue.Num());
		for (auto Pair = NativeValue.CreateIterator(); Pair; ++Pair)
		{
			void* Data = NativeValue.GetPairPtr(Pair);
			FOpResult EntryKey = VNativeRef::Get(Context, Data, MapProperty->KeyProp);
			if (!EntryKey.IsReturn())
			{
				V_DIE_UNLESS(EntryKey.IsError());
				return EntryKey;
			}
			FOpResult EntryValue = VNativeRef::Get(Context, Data, MapProperty->ValueProp);
			if (!EntryValue.IsReturn())
			{
				V_DIE_UNLESS(EntryValue.IsError());
				return EntryValue;
			}
			Pairs.Push({EntryKey.Value, EntryValue.Value});
		}

		V_RETURN(VMapBase::New<VMap>(Context, Pairs.Num(), [&Pairs](uint32 I) { return Pairs[I]; }));
	}
	else if (FOptionalProperty* OptionProperty = CastField<FOptionalProperty>(Property))
	{
		const void* NativeValue = OptionProperty->ContainerPtrToValuePtr<void>(Container);
		if (OptionProperty->IsSet(NativeValue))
		{
			FOpResult Inner = VNativeRef::Get(Context, NativeValue, OptionProperty->GetValueProperty());
			if (!Inner.IsReturn())
			{
				V_DIE_UNLESS(Inner.IsError());
				return Inner;
			}
			V_RETURN(VOption::New(Context, Inner.Value));
		}
		else
		{
			V_RETURN(GlobalFalse());
		}
	}

	// We couldn't handle this type
#if WITH_EDITORONLY_DATA
	// See if it's a legacy type
	VValue LegacyResult = FVRestValueProperty::ConvertToVValueLegacy(Context, Container, Property);
	V_DIE_UNLESS(LegacyResult);
	V_RETURN(LegacyResult);
#else
	VERSE_UNREACHABLE();
#endif
}

FOpResult VNativeRef::GetStruct(FAllocationContext Context, const void* Data, UScriptStruct* Struct)
{
	VClass* Class = nullptr;
	VShape* Shape = nullptr;
	if (UVerseStruct* UeStruct = Cast<UVerseStruct>(Struct))
	{
		if (UeStruct->IsTuple())
		{
			uint32 NumElements = 0;
			for (TFieldIterator<FProperty> Counter(UeStruct); Counter; ++Counter)
			{
				++NumElements;
			}
			TFieldIterator<FProperty> Iterator(UeStruct);
			// We assume here that the element initializer gets invoked in ascending index order.
			VArray& Array = VArray::New(Context, NumElements, [Context, Data, &Iterator](uint32 Index) {
				FOpResult TupleElem = VNativeRef::Get(Context, Data, *Iterator);
				++Iterator;
				if (!TupleElem.IsReturn())
				{
					// When a runtime error occurs, we can immediately halt VArray construction by returning an
					// uninitialized VValue.
					V_DIE_UNLESS(TupleElem.IsError());
					return VValue();
				}
				return TupleElem.Value;
			});
			if (NumElements > 0 && Array.GetArrayType() == EArrayType::None)
			{
				// If the array should have elements but has no backing store, a runtime error occurred mid-construction.
				return {FOpResult::Error};
			}
			V_RETURN(Array);
		}

		Class = UeStruct->Class.Get();
		Shape = &UeStruct->Shape.Get(Context).StaticCast<VShape>();
	}
	else
	{
		VNamedType* ImportedType = GlobalProgram->LookupImport(Context, Struct);
		if (ImportedType)
		{
			Class = &ImportedType->StaticCast<VClass>();
		}
	}

	if (!Class)
	{
		return {FOpResult::Fail};
	}

	if (Class->IsNativeRepresentation())
	{
		VEmergentType& EmergentType = Class->GetOrCreateEmergentTypeForNativeStruct(Context);
		VNativeStruct& NativeStruct = VNativeStruct::NewUninitialized(Context, EmergentType);
		Struct->CopyScriptStruct(NativeStruct.GetStruct(), Data);
		V_RETURN(NativeStruct);
	}

	V_DIE_UNLESS(Shape); // Must have a shape at this point
	TArray<VArchetype::VEntry> ArchetypeEntries;
	ArchetypeEntries.Reserve(Shape->GetNumFields());
	for (auto ShapeEntry = Shape->CreateFieldsIterator(); ShapeEntry; ++ShapeEntry)
	{
		ArchetypeEntries.Add(VArchetype::VEntry::ObjectField(Context, *ShapeEntry->Key));
	}
	VArchetype& Archetype = VArchetype::New(Context, VValue(), ArchetypeEntries);
	VValueObject& ValueStruct = Class->NewVObject(Context, Archetype);
	for (auto ShapeEntry = Shape->CreateFieldsIterator(); ShapeEntry; ++ShapeEntry)
	{
		bool bCreated = ValueStruct.CreateField(Context, *ShapeEntry->Key);
		V_DIE_UNLESS(bCreated);
		V_DIE_UNLESS(ShapeEntry->Value.Type == EFieldType::FProperty || ShapeEntry->Value.Type == EFieldType::FVerseProperty); // Shapes of UStructs must have only properties
		FOpResult Value = VNativeRef::Get(Context, Data, ShapeEntry->Value.UProperty);
		if (!Value.IsReturn())
		{
			V_DIE_UNLESS(Value.IsError());
			return Value;
		}
		FOpResult Result = ValueStruct.SetField(Context, *ShapeEntry->Key, Value.Value);
		V_DIE_UNLESS(Result.IsReturn());
	}
	V_RETURN(ValueStruct);
}

FOpResult VNativeRef::Set(FAllocationContext Context, VValue Value)
{
	if (UObject* Object = Base.Get().ExtractUObject())
	{
		V_DIE_UNLESS(Type == EType::FProperty);
		return Set<true>(Context, Object, Object, UProperty, Value);
	}
	else if (VNativeStruct* Struct = Base.Get().DynamicCast<VNativeStruct>())
	{
		V_DIE_UNLESS(Type == EType::FProperty);
		return Set<true>(Context, Struct, Struct->GetStruct(), UProperty, Value);
	}
	else
	{
		VERSE_UNREACHABLE();
	}
}

FOpResult VNativeRef::SetNonTransactionally(FAllocationContext Context, VValue Value)
{
	if (UObject* Object = Base.Get().ExtractUObject())
	{
		V_DIE_UNLESS(Type == EType::FProperty);
		return Set<false>(Context, nullptr, Object, UProperty, Value);
	}
	else if (VNativeStruct* Struct = Base.Get().DynamicCast<VNativeStruct>())
	{
		V_DIE_UNLESS(Type == EType::FProperty);
		return Set<false>(Context, nullptr, Struct->GetStruct(), UProperty, Value);
	}
	else
	{
		VERSE_UNREACHABLE();
	}
}

template FOpResult VNativeRef::Set<true>(FAllocationContext Context, UObject* Base, void* Container, FProperty* Property, VValue Value);
template FOpResult VNativeRef::Set<true>(FAllocationContext Context, VNativeStruct* Base, void* Container, FProperty* Property, VValue Value);
template FOpResult VNativeRef::Set<false>(FAllocationContext Context, std::nullptr_t Base, void* Container, FProperty* Property, VValue Value);

#define OP_RESULT_HELPER(Result) \
	if (!Result.IsReturn())      \
	{                            \
		return Result;           \
	}

namespace
{
template <bool bTransactional, typename BaseType, typename FunctionType>
FOpResult WriteImpl(FAllocationContext Context, BaseType Root, FunctionType F)
{
	if constexpr (bTransactional)
	{
		AutoRTFM::EContextStatus Status = AutoRTFM::Close(F);
		if (Status != AutoRTFM::EContextStatus::OnTrack)
		{
			Context.RaiseVerseRuntimeError(ERuntimeDiagnostic::ErrRuntime_NativeInternal, FText::FromString("Closed write to native field did not yield AutoRTFM::EContextStatus::OnTrack"));
			return {FOpResult::Error};
		}
	}
	else
	{
		F();
	}

	return {FOpResult::Return};
}

template <bool bTransactional, typename BaseType, typename ValueType, typename PropertyType>
FOpResult SetImpl(FAllocationContext Context, BaseType Base, void* Container, PropertyType* Property, VValue Value)
{
	AutoRTFM::UnreachableIfClosed("#jira SOL-8415");

	TFromVValue<ValueType> NativeValue;
	FOpResult Result = FNativeConverter::FromVValue(Context, Value, NativeValue);
	OP_RESULT_HELPER(Result);

	return WriteImpl<bTransactional>(Context, Base, [Property, Container, &NativeValue] {
		ValueType* ValuePtr = Property->template ContainerPtrToValuePtr<ValueType>(Container);
		*ValuePtr = NativeValue.GetValue();
	});
}
} // namespace

template <bool bTransactional, typename BaseType>
FOpResult VNativeRef::Set(FAllocationContext Context, BaseType Base, void* Container, FProperty* Property, VValue Value)
{
	AutoRTFM::UnreachableIfClosed("#jira SOL-8415");

	if (FVRestValueProperty* ValueProperty = CastField<FVRestValueProperty>(Property))
	{
		// Native field with VRestValue type: any, comparable, persistable, type, function
		VRestValue* NativeValue = ValueProperty->ContainerPtrToValuePtr<VRestValue>(const_cast<void*>(Container));
		return WriteImpl<bTransactional>(Context, Base, [Context, Value, NativeValue] {
			NativeValue->Set(Context, Value);
		});
	}
	else if (FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
	{
		UEnum* UeEnum = EnumProperty->GetEnum();
		if (UeEnum == StaticEnum<EVerseTrue>())
		{
			return SetImpl<bTransactional, BaseType, EVerseTrue>(Context, Base, Container, EnumProperty, Value);
		}

		V_REQUIRE_CONCRETE(Value);
		V_DIE_UNLESS(Value.IsCellOfType<VEnumerator>() && EnumProperty->GetUnderlyingProperty()->IsA<FByteProperty>());

		VEnumerator& Enumerator = Value.StaticCast<VEnumerator>();
		uint8 NativeValue = static_cast<uint8>(Enumerator.GetIntValue());
		if (NativeValue != Enumerator.GetIntValue())
		{
			Context.RaiseVerseRuntimeError(ERuntimeDiagnostic::ErrRuntime_NativeInternal, FText::FromString("Native enumerators must be integers between 0 and 255"));
			return {FOpResult::Error};
		}
		return WriteImpl<bTransactional>(Context, Base, [EnumProperty, Container, NativeValue] {
			*EnumProperty->ContainerPtrToValuePtr<uint8>(Container) = NativeValue;
		});
	}
	else if (FBoolProperty* LogicProperty = CastField<FBoolProperty>(Property))
	{
		return SetImpl<bTransactional, BaseType, bool>(Context, Base, Container, LogicProperty, Value);
	}
	if (FInt64Property* IntProperty = CastField<FInt64Property>(Property))
	{
		return SetImpl<bTransactional, BaseType, int64>(Context, Base, Container, IntProperty, Value);
	}
	else if (FDoubleProperty* FloatProperty = CastField<FDoubleProperty>(Property))
	{
		return SetImpl<bTransactional, BaseType, double>(Context, Base, Container, FloatProperty, Value);
	}
	else if (FByteProperty* CharProperty = CastField<FByteProperty>(Property))
	{
		return SetImpl<bTransactional, BaseType, UTF8CHAR>(Context, Base, Container, CharProperty, Value);
	}
	else if (FIntProperty* Char32Property = CastField<FIntProperty>(Property))
	{
		return SetImpl<bTransactional, BaseType, UTF32CHAR>(Context, Base, Container, Char32Property, Value);
	}
	else if (FClassProperty* TypeProperty = CastField<FClassProperty>(Property))
	{
		// VerseVM does not use FClassProperty or FVerseClassProperty- fall through to legacy conversion.
	}
	else if (FObjectProperty* ClassProperty = CastField<FObjectProperty>(Property))
	{
		if (ClassProperty->PropertyClass == UStruct::StaticClass())
		{
			// VerseVM does not use FObjectProperty for types- fall through to legacy conversion.
		}
		else
		{
			// Convert as TNonNullPtr<UObject> but write as TNonNullPtr<TObjectPtr<UObject>> for the write barrier.

			TFromVValue<TNonNullPtr<UObject>> NativeValue;
			FOpResult Result = FNativeConverter::FromVValue(Context, Value, NativeValue);
			OP_RESULT_HELPER(Result);

			return WriteImpl<bTransactional>(Context, Base, [ClassProperty, Container, &NativeValue] {
				TNonNullPtr<TObjectPtr<UObject>>* ValuePtr = ClassProperty->template ContainerPtrToValuePtr<TNonNullPtr<TObjectPtr<UObject>>>(Container);
				*ValuePtr = NativeValue.GetValue().Get();
			});
		}
	}
	else if (FStructProperty* StructProperty = CastField<FStructProperty>(Property))
	{
		V_REQUIRE_CONCRETE(Value);

		if (StructProperty->Struct == FVerseRational::StaticStruct())
		{
			return SetImpl<bTransactional, BaseType, FVerseRational>(Context, Base, Container, StructProperty, Value);
		}

		void* ValuePtr = StructProperty->ContainerPtrToValuePtr<void>(Container);
		auto WriteStruct = [Context, Base, StructProperty, ValuePtr](const auto& F) -> FOpResult {
			UScriptStruct* ScriptStruct = StructProperty->Struct;

			// Write the converted struct into temporary storage.
			TArray<std::byte, TInlineAllocator<64>> TempStorage;
			TempStorage.AddUninitialized(ScriptStruct->GetStructureSize() + ScriptStruct->GetMinAlignment() - 1);
			std::byte* AlignedTempStorage = Align(TempStorage.GetData(), ScriptStruct->GetMinAlignment());
			StructProperty->InitializeValue(AlignedTempStorage);
			FOpResult InitResult = F(AlignedTempStorage);
			ON_SCOPE_EXIT
			{
				StructProperty->DestroyValue(AlignedTempStorage);
			};
			OP_RESULT_HELPER(InitResult);

			// Copy the converted struct to the final destination, transactionally if necessary.
			return WriteImpl<bTransactional>(Context, Base, [StructProperty, ValuePtr, AlignedTempStorage] {
				StructProperty->CopyCompleteValue(ValuePtr, AlignedTempStorage);
			});
		};

		VClass* Class = nullptr;
		VShape* Shape = nullptr;
		if (UVerseStruct* VerseStruct = Cast<UVerseStruct>(StructProperty->Struct))
		{
			if (VerseStruct->IsTuple())
			{
				VArrayBase& Array = Value.StaticCast<VArrayBase>();
				return WriteStruct([Context, &Array, VerseStruct](void* Dest) -> FOpResult {
					TFieldIterator<FProperty> Iterator(VerseStruct);
					for (int32 Index = 0; Index < Array.Num(); ++Index, ++Iterator)
					{
						FOpResult ElemResult = VNativeRef::Set<false>(Context, nullptr, Dest, *Iterator, Array.GetValue(Index));
						OP_RESULT_HELPER(ElemResult);
					}
					return {FOpResult::Return};
				});
			}

			Class = VerseStruct->Class.Get();
			Shape = &VerseStruct->Shape.Get(Context).StaticCast<VShape>();
		}
		else
		{
			VNamedType* ImportedType = GlobalProgram->LookupImport(Context, StructProperty->Struct);
			V_DIE_UNLESS(ImportedType);
			Class = &ImportedType->StaticCast<VClass>();
		}

		if (Class->IsNativeRepresentation())
		{
			VNativeStruct& NativeStruct = Value.StaticCast<VNativeStruct>();
			checkSlow(VNativeStruct::GetUScriptStruct(*NativeStruct.GetEmergentType()) == StructProperty->Struct);

			return WriteImpl<bTransactional>(Context, Base, [StructProperty, ValuePtr, &NativeStruct] {
				StructProperty->CopyCompleteValue(ValuePtr, NativeStruct.GetStruct());
			});
		}

		V_DIE_UNLESS(Shape);
		VValueObject& ValueStruct = Value.StaticCast<VValueObject>();
		return WriteStruct([Context, &ValueStruct, StructProperty, Shape](void* Dest) -> FOpResult {
			for (auto ShapeEntry = Shape->CreateFieldsIterator(); ShapeEntry; ++ShapeEntry)
			{
				FOpResult LoadResult = ValueStruct.LoadField(Context, *ShapeEntry->Key);
				OP_RESULT_HELPER(LoadResult);
				V_DIE_UNLESS(ShapeEntry->Value.Type == EFieldType::FProperty || ShapeEntry->Value.Type == EFieldType::FVerseProperty);
				FOpResult WriteResult = VNativeRef::Set<false>(Context, nullptr, Dest, ShapeEntry->Value.UProperty, LoadResult.Value);
				OP_RESULT_HELPER(WriteResult);
			}
			return {FOpResult::Return};
		});
	}
	else if (FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
	{
		V_REQUIRE_CONCRETE(Value);
		V_DIE_UNLESS(Value.IsCellOfType<VArrayBase>());
		VArrayBase& Array = Value.StaticCast<VArrayBase>();

		FScriptArray NativeValue;
		FScriptArrayHelper Helper(ArrayProperty, &NativeValue);
		FOpResult Result = WriteImpl<bTransactional>(Context, nullptr, [&] { Helper.EmptyAndAddValues(Array.Num()); });
		OP_RESULT_HELPER(Result);
		for (int32 Index = 0; Index < Array.Num(); Index++)
		{
			FOpResult ElemResult = VNativeRef::Set<false>(Context, nullptr, Helper.GetElementPtr(Index), ArrayProperty->Inner, Array.GetValue(Index));
			OP_RESULT_HELPER(ElemResult);
		}

		return WriteImpl<bTransactional>(Context, Base, [ArrayProperty, Container, &NativeValue] {
			FScriptArrayHelper_InContainer ValuePtr(ArrayProperty, Container);
			ValuePtr.MoveAssign(&NativeValue);
		});
	}
	else if (FVerseStringProperty* StringProperty = CastField<FVerseStringProperty>(Property))
	{
		return SetImpl<bTransactional, BaseType, FNativeString>(Context, Base, Container, StringProperty, Value);
	}
	else if (FMapProperty* MapProperty = CastField<FMapProperty>(Property))
	{
		V_REQUIRE_CONCRETE(Value);
		V_DIE_UNLESS(Value.IsCellOfType<VMapBase>());
		VMapBase& Map = Value.StaticCast<VMapBase>();

		FScriptMap NativeValue;
		FScriptMapHelper Helper(MapProperty, &NativeValue);
		FOpResult Result = WriteImpl<bTransactional>(Context, nullptr, [&] { Helper.EmptyValues(Map.Num()); });
		OP_RESULT_HELPER(Result);
		for (TPair<VValue, VValue> Pair : Map)
		{
			int32 Index = Helper.AddDefaultValue_Invalid_NeedsRehash();
			FOpResult KeyResult = VNativeRef::Set<false>(Context, nullptr, Helper.GetPairPtr(Index), Helper.GetKeyProperty(), Pair.Key);
			OP_RESULT_HELPER(KeyResult);
			FOpResult ValueResult = VNativeRef::Set<false>(Context, nullptr, Helper.GetPairPtr(Index), Helper.GetValueProperty(), Pair.Value);
			OP_RESULT_HELPER(ValueResult);
		}
		Helper.Rehash();

		return WriteImpl<bTransactional>(Context, Base, [MapProperty, Container, &NativeValue] {
			FScriptMapHelper_InContainer ValuePtr(MapProperty, Container);
			ValuePtr.MoveAssign(&NativeValue);
		});
	}
	else if (FOptionalProperty* OptionProperty = CastField<FOptionalProperty>(Property))
	{
		V_REQUIRE_CONCRETE(Value);

		if (VOption* Option = Value.DynamicCast<VOption>())
		{
			void* Data;
			FOpResult Result = WriteImpl<bTransactional>(Context, Base, [OptionProperty, Container, Value, &Data] {
				void* ValuePtr = OptionProperty->ContainerPtrToValuePtr<void>(Container);
				Data = OptionProperty->MarkSetAndGetInitializedValuePointerToReplace(ValuePtr);
			});
			OP_RESULT_HELPER(Result);

			return VNativeRef::Set<bTransactional>(Context, Base, Data, OptionProperty->GetValueProperty(), Option->GetValue());
		}
		else
		{
			V_DIE_UNLESS(Value == GlobalFalse());

			return WriteImpl<bTransactional>(Context, Base, [OptionProperty, Container] {
				void* ValuePtr = OptionProperty->ContainerPtrToValuePtr<void>(Container);
				OptionProperty->MarkUnset(ValuePtr);
			});
		}
	}

	// We couldn't handle this type
#if WITH_EDITORONLY_DATA
	// See if it's a legacy type
	return WriteImpl<bTransactional>(Context, Base, [Context, Container, Property, Value] {
		if (!FVRestValueProperty::ConvertFromVValueLegacy(Context, Container, Property, Value))
		{
			V_DIE_UNLESS(false);
		}
	});
#else
	VERSE_UNREACHABLE();
#endif
}

#undef OP_RESULT_HELPER

FOpResult VNativeRef::FreezeImpl(FAllocationContext Context, VTask*, FOp* AwaitPC)
{
	return Get(Context);
}

template <typename TVisitor>
void VNativeRef::VisitReferencesImpl(TVisitor& Visitor)
{
	Visitor.Visit(Base, TEXT("Base"));
}

} // namespace Verse
#endif

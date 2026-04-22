// Copyright Epic Games, Inc. All Rights Reserved.

#include "UObject/VerseValueProperty.h"
#include "UObject/GarbageCollectionSchema.h"
#include "UObject/InstanceDataObjectUtils.h"
#include "UObject/ObjectInstancingGraph.h"
#include "VerseVM/Inline/VVMEnterVMInline.h"
#include "VerseVM/Inline/VVMRefInline.h"
#include "VerseVM/Inline/VVMValueInline.h"
#include "VerseVM/VVMAbstractVisitor.h"
#include "VerseVM/VVMCell.h"
#include "VerseVM/VVMEngineEnvironment.h"
#include "VerseVM/VVMIntType.h"
#include "VerseVM/VVMRestValue.h"
#include "VerseVM/VVMStructuredArchiveVisitor.h"
#include "VerseVM/VVMValue.h"
#include "VerseVM/VVMJson.h"
#include "VerseVM/VVMWriteBarrier.h"
#if WITH_EDITORONLY_DATA
#include "VerseVM/VBPVMDynamicProperty.h"
#endif

#if WITH_VERSE_VM || defined(__INTELLISENSE__)

FVCellProperty::FVCellProperty(FFieldVariant InOwner, const FName& InName, EObjectFlags InObjectFlags)
	: Super(InOwner, InName, InObjectFlags)
{
}

FVCellProperty::FVCellProperty(FFieldVariant InOwner, const UECodeGen_Private::FVerseValuePropertyParams& Prop)
	: Super(InOwner, Prop)
{
}

FVValueProperty::FVValueProperty(FFieldVariant InOwner, const FName& InName, EObjectFlags InObjectFlags)
	: Super(InOwner, InName, InObjectFlags)
{
}

FVValueProperty::FVValueProperty(FFieldVariant InOwner, const UECodeGen_Private::FVerseValuePropertyParams& Prop)
	: Super(InOwner, Prop)
{
}

FVRestValueProperty::FVRestValueProperty(FFieldVariant InOwner, const FName& InName, EObjectFlags InObjectFlags)
	: Super(InOwner, InName, InObjectFlags)
{
}

FVRestValueProperty::FVRestValueProperty(FFieldVariant InOwner, const UECodeGen_Private::FVerseValuePropertyParams& Prop)
	: Super(InOwner, Prop)
{
}

FVRestValueProperty::FVRestValueProperty(FFieldVariant InOwner, const UECodeGen_Private::FVerseClassPropertyParams& Prop)
	: Super(InOwner, reinterpret_cast<const UECodeGen_Private::FClassPropertyParams&>(Prop))
{
}

FVRestValueProperty::~FVRestValueProperty()
{
	if (LegacyProperty)
	{
		delete LegacyProperty;
		LegacyProperty = nullptr;
	}
}

void FVRestValueProperty::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedVerseValue(Type);
}

FProperty* FVRestValueProperty::GetOrCreateLegacyProperty(Verse::FAllocationContext Context)
{
	if (LegacyProperty == nullptr)
	{
		AutoRTFM::Open([&] {
			Verse::IEngineEnvironment* Environment = Verse::VerseVM::GetEngineEnvironment();
			LegacyProperty = Environment->CreateLegacyProperty(Context, this);

			FArchive Ar;
			LegacyProperty->Link(Ar);
		});
	}

	return LegacyProperty;
}

void FVRestValueProperty::PostDuplicate(const FField& InField)
{
	Verse::FAllocationContext Context = Verse::FAllocationContextPromise{};
	const FVRestValueProperty& Source = static_cast<const FVRestValueProperty&>(InField);
	Type.Set(Context, Source.Type.Get());
	if (Source.LegacyProperty)
	{
		LegacyProperty = CastFieldChecked<FProperty>(FField::Duplicate(Source.LegacyProperty, this));
	}
#if WITH_EDITORONLY_DATA
	bNativeRepresentation = Source.bNativeRepresentation;
#endif
	Super::PostDuplicate(InField);
}

#if WITH_EDITORONLY_DATA

Verse::VValue FVRestValueProperty::ConvertToVValueLegacy(Verse::FAllocationContext Context, const void* Container, FProperty* Property)
{
	if (FVerseDynamicProperty* DynamicProperty = CastField<FVerseDynamicProperty>(Property))
	{
		const UE::FDynamicallyTypedValue* LegacyValue = DynamicProperty->ContainerPtrToValuePtr<UE::FDynamicallyTypedValue>(Container);
		return LegacyValue->ToVValue(Context);
	}

	// Convert type values.  `subtype` with class or interface upper bound and
	// `subtype` variants use `FVerseClassProperty`, while all other type values
	// use nullable `FObjectProperty`.  Note `FVerseClassProperty` inherits
	// (transitively) from `FObjectProperty`.
	if (FObjectProperty* TypeProperty = CastField<FObjectProperty>(Property))
	{
		UObject* Object = TypeProperty->GetObjectPtrPropertyValue_InContainer(Container);
		Verse::FNativeType NativeType(CastChecked<UStruct>(Object));
		return *NativeType.Type;
	}

	return Verse::VValue();
}

bool FVRestValueProperty::ConvertFromVValueLegacy(Verse::FAllocationContext Context, void* Container, FProperty* Property, Verse::VValue Value)
{
	if (FVerseDynamicProperty* DynamicProperty = CastField<FVerseDynamicProperty>(Property))
	{
		// Leave dynamic values default-initialized. Their deserialization code ignores the old value.
		return true;
	}

	if (FObjectProperty* TypeProperty = CastField<FObjectProperty>(Property))
	{
		// Leave non-class/struct types default-initialized. BPVM data cannot represent them anyway.
		if (Verse::VClass* Class = Value.DynamicCast<Verse::VClass>())
		{
			TypeProperty->SetObjectPropertyValue_InContainer(Container, Class->GetUETypeChecked<UObject>());
		}
		return true;
	}

	return false;
}

EConvertFromTypeResult FVRestValueProperty::ConvertFromType(const FPropertyTag& Tag, FStructuredArchive::FSlot Slot, uint8* Data, const UStruct* DefaultsStruct, const uint8* Defaults)
{
	if (Tag.Type == GetID())
	{
		return EConvertFromTypeResult::UseSerializeItem;
	}

	Verse::FRunningContext Context = Verse::FRunningContextPromise{};

	// Generate the corresponding BPVM property if necessary.
	GetOrCreateLegacyProperty(Context);

	// Do we know how to deserialize this tag?
	// Note that this type comparison is shallow and does not verify inner types for compatibility
	if (LegacyProperty && LegacyProperty->CanSerializeFromTypeName(Tag.GetType()))
	{
		V_DIE_UNLESS(LegacyProperty->GetElementSize() > 0 && LegacyProperty->GetElementSize() <= 1024);

		void* DeserializedData = FMemory_Alloca_Aligned(LegacyProperty->GetElementSize(), LegacyProperty->GetMinAlignment());
		LegacyProperty->InitializeValue(DeserializedData);

		// Convert the default value back to a legacy value in the destination buffer.
		// This is required for things like structs that delta serialize against the default.
		// When there is no default, leave the buffer default-initialized.
		Verse::VRestValue& RestValue = *GetPropertyValuePtr_InContainer(Data, Tag.ArrayIndex);
		if (!RestValue.IsUninitialized())
		{
			Verse::VValue Value = RestValue.Get(Context);
			if (Verse::VRef* Ref = Value.DynamicCast<Verse::VRef>())
			{
				Value = Ref->Get(Context);
			}

			Verse::FOpResult Result = Verse::VNativeRef::Set<false>(Context, nullptr, DeserializedData, LegacyProperty, Value);
			V_DIE_UNLESS(Result.IsReturn());
		}

		if (Tag.Type == NAME_BoolProperty)
		{
			// Bool properties are stored directly in the tag and serialize no extra data
			check(LegacyProperty->GetElementSize() == 1);
			*(bool*)DeserializedData = !!Tag.BoolVal;
		}
		else
		{
			// Serialize the legacy data. If Defaults is provided, convert it in the same way as Data above.
			if (Defaults)
			{
				void* DefaultsData = FMemory_Alloca_Aligned(LegacyProperty->GetElementSize(), LegacyProperty->GetMinAlignment());
				LegacyProperty->InitializeValue(DefaultsData);

				Verse::VRestValue& DefaultRestValue = *GetPropertyValuePtr_InContainer(const_cast<uint8*>(Defaults), Tag.ArrayIndex);
				if (!DefaultRestValue.IsUninitialized())
				{
					Verse::VValue Value = DefaultRestValue.Get(Context);
					if (Verse::VRef* Ref = Value.DynamicCast<Verse::VRef>())
					{
						Value = Ref->Get(Context);
					}

					Verse::FOpResult Result = Verse::VNativeRef::Set<false>(Context, nullptr, DefaultsData, LegacyProperty, Value);
					V_DIE_UNLESS(Result.IsReturn());
				}

				LegacyProperty->SerializeItem(Slot, DeserializedData, DefaultsData);

				LegacyProperty->DestroyValue(DefaultsData);
			}
			else
			{
				LegacyProperty->SerializeItem(Slot, DeserializedData, nullptr);
			}
		}

		// Convert legacy to VValue
		Verse::FOpResult Result = Verse::VNativeRef::Get(Context, DeserializedData, LegacyProperty);
		V_DIE_UNLESS(Result.IsReturn());

		// Clean up
		LegacyProperty->DestroyValue(DeserializedData);

		// Store in this property's data value
		if (!bNativeRepresentation && Type.Follow().IsCellOfType<Verse::VPointerType>())
		{
			if (RestValue.IsUninitialized())
			{
				Verse::VRef& NewRef = Verse::VRef::New(Context);
				NewRef.SetNonTransactionally(Context, Result.Value);
				RestValue.Set(Context, NewRef);
			}
			else
			{
				Verse::VRef& Ref = RestValue.Get(Context).StaticCast<Verse::VRef>();
				Ref.SetNonTransactionally(Context, Result.Value);
			}
		}
		else
		{
			RestValue.Set(Context, Result.Value);
		}

		return EConvertFromTypeResult::Serialized;
	}

	// Else, return default which will either deserialize a FVRestValueProperty, or trigger an error
	return EConvertFromTypeResult::UseSerializeItem;
}

#endif

void FVRestValueProperty::CopyValuesInternal(void* Dest, const void* Src, int32 Count) const
{
	const bool bWasClosed = AutoRTFM::IsClosed();
	AutoRTFM::Open([&] {
		Verse::FRunningContext Context = Verse::FRunningContextPromise{};
		for (int32 Index = 0; Index < Count; Index++)
		{
			Verse::VRestValue& DestSlot = TTypeFundamentals::GetPropertyValuePtr(Dest)[Index];
			Verse::VRestValue& SrcSlot = TTypeFundamentals::GetPropertyValuePtr(const_cast<void*>(Src))[Index];
			V_DIE_IF(DestSlot.CanDefQuickly() || SrcSlot.CanDefQuickly());

			Verse::VValue SrcValue = SrcSlot.Get(Context);
			if (Verse::VRef* SrcRef = SrcValue.DynamicCast<Verse::VRef>())
			{
				Verse::FOpResult Result = Verse::VValue::Freeze(Context, SrcRef->Get(Context), nullptr, nullptr);
				V_DIE_UNLESS(Result.IsReturn());
				SrcValue = Verse::VValue::Melt(Context, Result.Value);

				Verse::VRef& DestRef = Verse::VRef::New(Context);
				DestRef.SetNonTransactionally(Context, SrcValue);
				SrcValue = DestRef;
			}

			if (bWasClosed)
			{
				DestSlot.SetTransactionally(Context, SrcValue);
			}
			else
			{
				DestSlot.Set(Context, SrcValue);
			}
		}
	});
}

namespace
{
Verse::FOpResult InstancePropertyValueTransactionally(FObjectInstancingGraph* InstanceGraph, UObject* SourceComponent, TNotNull<UObject*> CurrentValue, TNotNull<UObject*> Owner)
{
	AutoRTFM::UnreachableIfClosed();

	UObject* Result = nullptr;
	AutoRTFM::EContextStatus Status = AutoRTFM::Close([&] {
		Result = InstanceGraph->InstancePropertyValue(SourceComponent, CurrentValue, Owner);
	});
	if (Status != AutoRTFM::EContextStatus::OnTrack)
	{
		return {Verse::FOpResult::Error};
	}
	V_RETURN(Result);
}

bool InstanceSubobjectTemplatesTransactionally(UStruct* Struct, TNotNull<void*> Data, const void* DefaultData, const UStruct* DefaultStruct, TNotNull<UObject*> Owner, FObjectInstancingGraph* InstanceGraph)
{
	AutoRTFM::UnreachableIfClosed();

	AutoRTFM::EContextStatus Status = AutoRTFM::Close([&] {
		Struct->InstanceSubobjectTemplates(Data, DefaultData, DefaultStruct, Owner, InstanceGraph);
	});
	return Status == AutoRTFM::EContextStatus::OnTrack;
}

Verse::FOpResult InstanceValue(Verse::FAllocationContext Context, bool bTransactional, bool bMutable, Verse::VValue Value, Verse::VValue DefaultValue, TNotNull<UObject*> Owner, FObjectInstancingGraph* InstanceGraph)
{
	AutoRTFM::UnreachableIfClosed();

	if (UObject* NativeObject = Value.ExtractUObject())
	{
		if (bTransactional)
		{
			return InstancePropertyValueTransactionally(InstanceGraph, DefaultValue.ExtractUObject(), NativeObject, Owner);
		}
		else
		{
			V_RETURN(InstanceGraph->InstancePropertyValue(DefaultValue.ExtractUObject(), NativeObject, Owner));
		}
	}
	else if (Verse::VNativeStruct* NativeStruct = Value.DynamicCast<Verse::VNativeStruct>())
	{
		Verse::VNativeStruct* DefaultStruct = DefaultValue.DynamicCast<Verse::VNativeStruct>();

		auto InstanceStruct = [Owner, InstanceGraph, bTransactional, DefaultStruct](Verse::VNativeStruct* Struct) {
			UScriptStruct* ScriptStruct = Verse::VNativeStruct::GetUScriptStruct(*Struct->GetEmergentType());
			if (bTransactional)
			{
				return InstanceSubobjectTemplatesTransactionally(ScriptStruct, Struct->GetStruct(), DefaultStruct ? DefaultStruct->GetStruct() : nullptr, ScriptStruct, Owner, InstanceGraph);
			}
			else
			{
				ScriptStruct->InstanceSubobjectTemplates(Struct->GetStruct(), DefaultStruct ? DefaultStruct->GetStruct() : nullptr, ScriptStruct, Owner, InstanceGraph);
				return true;
			}
		};
		if (bMutable)
		{
			if (!InstanceStruct(NativeStruct))
			{
				return {Verse::FOpResult::Error};
			}
			V_RETURN(*NativeStruct);
		}
		else
		{
			// InstanceSubobjectTemplates mutates the struct in place. It is immutable, so clone it first.
			Verse::VValue MeltedStruct = Verse::VValue::Melt(Context, *NativeStruct);
			if (!InstanceStruct(&MeltedStruct.StaticCast<Verse::VNativeStruct>()))
			{
				return {Verse::FOpResult::Error};
			}
			Verse::FOpResult FreezeResult = Verse::VValue::Freeze(Context, MeltedStruct, nullptr, nullptr);
			V_DIE_UNLESS(FreezeResult.IsReturn());
			return FreezeResult;
		}
	}
	else if (Verse::VValueObject* Struct = Value.DynamicCast<Verse::VValueObject>(); Struct && Struct->IsStruct())
	{
		Verse::VValueObject* DefaultStruct = DefaultValue.DynamicCast<Verse::VValueObject>();

		auto InstanceStruct = [Context, bTransactional, Owner, InstanceGraph, DefaultStruct](Verse::VValueObject* Struct) {
			Verse::VEmergentType& EmergentType = *Struct->GetEmergentType();
			for (auto It = EmergentType.Shape->CreateFieldsIterator(); It; ++It)
			{
				Verse::FOpResult LoadResult = Struct->LoadField(Context, *It->Key);
				V_DIE_UNLESS(LoadResult.IsReturn());

				Verse::VValue DefaultField;
				if (DefaultStruct)
				{
					Verse::FOpResult DefaultResult = DefaultStruct->LoadField(Context, *It->Key);
					V_DIE_UNLESS(DefaultResult.IsReturn());
					DefaultField = DefaultResult.Value;
				}

				Verse::FOpResult InstanceResult = InstanceValue(Context, bTransactional, /*bMutable*/ true, LoadResult.Value, DefaultField, Owner, InstanceGraph);
				if (!InstanceResult.IsReturn())
				{
					return false;
				}
				Verse::VValue Field = InstanceResult.Value;

				Verse::FOpResult StoreResult = Struct->SetField(Context, *It->Key, Field);
				V_DIE_UNLESS(StoreResult.IsReturn());
			}
			return true;
		};
		if (bMutable)
		{
			if (!InstanceStruct(Struct))
			{
				return {Verse::FOpResult::Error};
			}
			V_RETURN(*Struct);
		}
		else
		{
			// InstanceStruct mutates the struct in place. It is immutable, so clone it first.
			Verse::VValue MeltedStruct = Verse::VValue::Melt(Context, *Struct);
			if (!InstanceStruct(&MeltedStruct.StaticCast<Verse::VValueObject>()))
			{
				return {Verse::FOpResult::Error};
			}
			Verse::FOpResult FreezeResult = Verse::VValue::Freeze(Context, MeltedStruct, nullptr, nullptr);
			V_DIE_UNLESS(FreezeResult.IsReturn());
			return FreezeResult;
		}
	}
	else if (Verse::VArrayBase* Array = Value.DynamicCast<Verse::VArrayBase>())
	{
		Verse::VArrayBase* DefaultArray = DefaultValue.DynamicCast<Verse::VArrayBase>();

		int32 ArrayNum = Array->Num();
		// TODO: Mutate in-place if bMutable?
		auto InstanceElement = [Context, bTransactional, bMutable, Owner, InstanceGraph, Array, DefaultArray](uint32 Index) {
			Verse::VValue DefaultElement = (DefaultArray && Index < DefaultArray->Num()) ? DefaultArray->GetValue(Index) : Verse::VValue();
			Verse::FOpResult Result = InstanceValue(Context, bTransactional, bMutable, Array->GetValue(Index), DefaultElement, Owner, InstanceGraph);
			if (!Result.IsReturn())
			{
				V_DIE_UNLESS(Result.IsError());
				return Verse::VValue();
			}
			return Result.Value;
		};
		if (bMutable)
		{
			Verse::VMutableArray& Result = Verse::VMutableArray::New(Context, Array->Num(), InstanceElement);
			if (ArrayNum > 0 && Result.GetArrayType() == Verse::EArrayType::None)
			{
				return {Verse::FOpResult::Error};
			}
			V_RETURN(Result);
		}
		else
		{
			Verse::VArray& Result = Verse::VArray::New(Context, Array->Num(), InstanceElement);
			if (ArrayNum > 0 && Result.GetArrayType() == Verse::EArrayType::None)
			{
				return {Verse::FOpResult::Error};
			}
			V_RETURN(Result);
		}
	}
	else if (Verse::VMapBase* Map = Value.DynamicCast<Verse::VMapBase>())
	{
		Verse::VMapBase* DefaultMap = DefaultValue.DynamicCast<Verse::VMapBase>();

		// TODO: Mutate in-place if bMutable? Can we mutate keys in place like FMapProperty does for TMap?
		TArray<TPair<Verse::VValue, Verse::VValue>> Pairs;
		Pairs.Reserve(Map->Num());
		for (TPair<Verse::VValue, Verse::VValue> Pair : *Map)
		{
			Verse::VMapBase::PairType* DefaultPair = nullptr;
			if (DefaultMap)
			{
				Verse::VMapBase::SequenceType Slot;
				if (DefaultMap->FindWithSlot(Context, Pair.Key, &Slot))
				{
					DefaultPair = &DefaultMap->GetPairTable()[Slot];
				}
			}

			Verse::FOpResult KeyResult = InstanceValue(Context, bTransactional, bMutable, Pair.Key, DefaultPair ? DefaultPair->Key.Get() : Verse::VValue(), Owner, InstanceGraph);
			if (!KeyResult.IsReturn())
			{
				return KeyResult;
			}
			Verse::VValue InstancedKey = KeyResult.Value;
			Verse::FOpResult ValueResult = InstanceValue(Context, bTransactional, bMutable, Pair.Value, DefaultPair ? DefaultPair->Value.Get() : Verse::VValue(), Owner, InstanceGraph);
			if (!ValueResult.IsReturn())
			{
				return ValueResult;
			}
			Verse::VValue InstancedValue = ValueResult.Value;
			Pairs.Push({InstancedKey, InstancedValue});
		}

		if (bMutable)
		{
			V_RETURN(Verse::VMapBase::New<Verse::VMutableMap>(Context, Pairs.Num(), [&Pairs](uint32 I) { return Pairs[I]; }));
		}
		else
		{
			V_RETURN(Verse::VMapBase::New<Verse::VMap>(Context, Pairs.Num(), [&Pairs](uint32 I) { return Pairs[I]; }));
		}
	}
	else if (Verse::VOption* Option = Value.DynamicCast<Verse::VOption>())
	{
		Verse::VOption* DefaultOption = DefaultValue.DynamicCast<Verse::VOption>();
		Verse::FOpResult Result = InstanceValue(Context, bTransactional, bMutable, Option->GetValue(), DefaultOption ? DefaultOption->GetValue() : Verse::VValue(), Owner, InstanceGraph);
		if (!Result.IsReturn())
		{
			return Result;
		}
		Verse::VValue OptionValue = Result.Value;
		if (OptionValue == Option->GetValue())
		{
			V_RETURN(*Option);
		}
		V_RETURN(Verse::VOption::New(Context, OptionValue));
	}
	else if (Verse::VFunction* Function = Value.DynamicCast<Verse::VFunction>())
	{
		Verse::VFunction* DefaultFunction = DefaultValue.DynamicCast<Verse::VFunction>();
		Verse::FOpResult Result = InstanceValue(Context, bTransactional, bMutable, Function->Self.Get(), DefaultFunction ? DefaultFunction->Self.Get() : Verse::VValue(), Owner, InstanceGraph);
		if (!Result.IsReturn())
		{
			return Result;
		}
		Verse::VValue Self = Result.Value;
		if (Self == Function->Self.Get())
		{
			V_RETURN(*Function);
		}
		V_RETURN(Verse::VFunction::New(Context, *Function->Procedure, Self, Function->ParentScope.Get()));
	}
	else if (Verse::VNativeFunction* NativeFunction = Value.DynamicCast<Verse::VNativeFunction>())
	{
		Verse::VNativeFunction* DefaultFunction = DefaultValue.DynamicCast<Verse::VNativeFunction>();
		Verse::FOpResult Result = InstanceValue(Context, bTransactional, bMutable, NativeFunction->Self.Get(), DefaultFunction ? DefaultFunction->Self.Get() : Verse::VValue(), Owner, InstanceGraph);
		if (!Result.IsReturn())
		{
			return Result;
		}
		Verse::VValue Self = Result.Value;
		if (Self == Function->Self.Get())
		{
			V_RETURN(*NativeFunction);
		}
		V_RETURN(Verse::VNativeFunction::New(Context, NativeFunction->NumPositionalParameters, NativeFunction->Thunk, *NativeFunction->Name, Self));
	}
	else
	{
		V_RETURN(Value);
	}
}
}

void FVRestValueProperty::InstanceSubobjects(void* Data, const void* DefaultData, TNotNull<UObject*> Owner, FObjectInstancingGraph* InstanceGraph)
{
	const bool bTransactional = AutoRTFM::IsClosed();
	AutoRTFM::Open([&] {
		Verse::FRunningContext Context = Verse::FRunningContextPromise{};
		for (int32 Index = 0; Index < ArrayDim; Index++)
		{
			Verse::VRestValue& Slot = TTypeFundamentals::GetPropertyValuePtr(Data)[Index];
			Verse::VRestValue* DefaultSlot = DefaultData ? &TTypeFundamentals::GetPropertyValuePtr(const_cast<void*>(DefaultData))[Index] : nullptr;

			if (Slot.IsUninitialized())
			{
				continue;
			}

			Verse::VValue Value = Slot.Get(Context);
			Verse::VValue DefaultValue = DefaultSlot ? DefaultSlot->Get(Context) : Verse::VValue();
			if (Verse::VRef* Ref = Value.DynamicCast<Verse::VRef>())
			{
				Verse::VRef* DefaultRef = DefaultValue.DynamicCast<Verse::VRef>();
				Verse::FOpResult Result = InstanceValue(Context, bTransactional, /*bMutable*/ true, Ref->Get(Context), DefaultRef ? DefaultRef->Get(Context) : Verse::VValue(), Owner, InstanceGraph);
				if (!Result.IsReturn())
				{
					V_DIE_UNLESS(Result.IsError());
					return;
				}
				Value = Result.Value;

				if (GUObjectArray.IsDisregardForGC(Owner))
				{
					Ref->AddRef(Context);
				}
				if (bTransactional)
				{
					Ref->Set(Context, Verse::VValue::Melt(Context, Value));
				}
				else
				{
					Ref->SetNonTransactionally(Context, Verse::VValue::Melt(Context, Value));
				}
			}
			else
			{
				Verse::FOpResult Result = InstanceValue(Context, bTransactional, /*bMutable*/ false, Value, DefaultValue, Owner, InstanceGraph);
				if (!Result.IsReturn())
				{
					V_DIE_UNLESS(Result.IsError());
					return;
				}
				Value = Result.Value;

				if (Value.IsCell() && GUObjectArray.IsDisregardForGC(Owner))
				{
					Value.AsCell().AddRef(Context);
				}
				if (bTransactional)
				{
					Slot.SetTransactionally(Context, Value);
				}
				else
				{
					Slot.Set(Context, Value);
				}
			}
		}
	});
}

template <typename T>
FString TFVersePropertyBase<T>::GetCPPMacroType(FString& ExtendedTypeText) const
{
	ExtendedTypeText = FString();
	return FString();
}

template <typename T>
bool TFVersePropertyBase<T>::Identical(const void* A, const void* B, uint32 PortFlags) const
{
	check(A);

	if (nullptr == B) // if the comparand is NULL, we just call this no-match
	{
		return false;
	}

	const TCppType* Lhs = reinterpret_cast<const TCppType*>(A);
	const TCppType* Rhs = reinterpret_cast<const TCppType*>(B);
	return *Lhs == *Rhs;
}

template <typename T>
void TFVersePropertyBase<T>::SerializeItem(FStructuredArchive::FSlot Slot, void* Value, void const* Defaults) const
{
	Verse::FRunningContext Context = Verse::FRunningContextPromise{};
	Verse::FStructuredArchiveVisitor Visitor(Context, Slot);
	Visitor.Visit(*static_cast<TCppType*>(Value), TEXT(""));
}

template <typename T>
void TFVersePropertyBase<T>::ExportText_Internal(FString& ValueStr, const void* PropertyValueOrContainer, EPropertyPointerType PointerType, const void* DefaultValue, UObject* Parent, int32 PortFlags, UObject* ExportRootScope) const
{
	check(false);
	return;
}

template <typename T>
const TCHAR* TFVersePropertyBase<T>::ImportText_Internal(const TCHAR* Buffer, void* ContainerOrPropertyPtr, EPropertyPointerType PropertyPointerType, UObject* OwnerObject, int32 PortFlags, FOutputDevice* ErrorText) const
{
	check(false);
	return TEXT("");
}

template <typename T>
bool TFVersePropertyBase<T>::ContainsObjectReference(TArray<const FStructProperty*>& EncounteredStructProps, EPropertyObjectReferenceType InReferenceType/* = EPropertyObjectReferenceType::Strong*/) const
{
	return true;
}

template <typename T>
void TFVersePropertyBase<T>::EmitReferenceInfo(UE::GC::FSchemaBuilder& Schema, int32 BaseOffset, TArray<const FStructProperty*>& EncounteredStructProps, UE::GC::FPropertyStack& DebugPath)
{
	for (int32 Idx = 0, Num = FProperty::ArrayDim; Idx < Num; ++Idx)
	{
		Schema.Add(UE::GC::DeclareMember(DebugPath, BaseOffset + FProperty::GetOffset_ForGC() + Idx * sizeof(TCppType), UE::GC::EMemberType::VerseValue));
	}
}

TSharedPtr<FJsonValue> VersePropertyToJSON(Verse::FRunningContext Context, const FProperty* Property, const void* InValue, TMap<const void*, Verse::EVisitState>& VisitedObjects, const Verse::VerseVMToJsonCallback& Callback, const uint32 RecursionDepth)
{
	if (const FVRestValueProperty* VRestValueProp = CastField<FVRestValueProperty>(Property))
	{
		const Verse::VRestValue* RestValue = static_cast<const Verse::VRestValue*>(InValue);
		return RestValue->ToJSON(Context, Verse::EValueJSONFormat::Analytics, VisitedObjects, Callback, RecursionDepth + 1);
	}
	if (const FVValueProperty* VValueProp = CastField<FVValueProperty>(Property))
	{
		const Verse::TWriteBarrier<Verse::VValue>* Value = static_cast<const Verse::TWriteBarrier<Verse::VValue>*>(InValue);
		return Value->Get().ToJSON(Context, Verse::EValueJSONFormat::Analytics, VisitedObjects, Callback, RecursionDepth + 1);
	}
	if (const FVCellProperty* VCellProp = CastField<FVCellProperty>(Property))
	{
		const Verse::TWriteBarrier<Verse::VCell>* Cell = static_cast<const Verse::TWriteBarrier<Verse::VCell>*>(InValue);
		return Cell->Get()->ToJSON(Context, Verse::EValueJSONFormat::Analytics, VisitedObjects, Callback, RecursionDepth + 1);
	}
	V_DIE("Could not convert Verse property to JSON - Unknown property type!");
}

IMPLEMENT_FIELD(FVCellProperty)
IMPLEMENT_FIELD(FVValueProperty)
IMPLEMENT_FIELD(FVRestValueProperty)

#endif // WITH_VERSE_VM
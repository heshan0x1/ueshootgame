// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_VERSE_VM || defined(__INTELLISENSE__)
#include "HAL/Platform.h"
#include "UObject/UnrealType.h"
#include "VerseVM/VVMRestValue.h"
#include "VerseVM/VVMValue.h"
#include "VerseVM/VVMWriteBarrier.h"
#elif WITH_VERSE_BPVM 
// When using the BPVM, we use a different property type for FVerseValue structs with UPROPERTY markup 
class FVerseDynamicProperty;
using FVerseValueProperty = FVerseDynamicProperty;
#endif 

#if WITH_VERSE_VM || defined(__INTELLISENSE__)

namespace Verse
{
struct VType;
}

class FVRestValueProperty;
// Note that the property type for FVerseValue is FVRestValueProperty and not FVValueProperty 
using FVerseValueProperty = FVRestValueProperty;

template <>
inline const TCHAR* TPropertyTypeFundamentals<Verse::TWriteBarrier<Verse::VCell>>::GetTypeName()
{
	return TEXT("Verse::TWriteBarrier<Verse::VCell>");
}

template <>
inline const TCHAR* TPropertyTypeFundamentals<Verse::TWriteBarrier<Verse::VValue>>::GetTypeName()
{
	return TEXT("Verse::TWriteBarrier<Verse::VValue>");
}

template <>
inline const TCHAR* TPropertyTypeFundamentals<Verse::VRestValue>::GetTypeName()
{
	return TEXT("Verse::VRestValue");
}

template <>
inline Verse::VRestValue TPropertyTypeFundamentals<Verse::VRestValue>::GetDefaultPropertyValue()
{
	Verse::VRestValue Value(0);
	Value.SetNonCellNorPlaceholder(Verse::VValue());
	return Value;
}

template <>
inline Verse::VRestValue* TPropertyTypeFundamentals<Verse::VRestValue>::InitializePropertyValue(void* A)
{
	Verse::VRestValue* Value = new (A) Verse::VRestValue(0);
	Value->SetNonCellNorPlaceholder(Verse::VValue());
	return Value;
}

// Template base class for the verse property types
template <typename InTCppType>
class TFVersePropertyBase : public TProperty<InTCppType, FProperty>
{
public:
	using Super = TProperty<InTCppType, FProperty>;
	using TCppType = InTCppType;

	TFVersePropertyBase(EInternal InInternal, FFieldClass* InClass)
		: Super(EC_InternalUseOnlyConstructor, InClass)
	{
	}

	TFVersePropertyBase(FFieldVariant InOwner, const FName& InName, EObjectFlags InObjectFlags)
		: Super(InOwner, InName, InObjectFlags)
	{
	}

	TFVersePropertyBase(FFieldVariant InOwner, const UECodeGen_Private::FVerseValuePropertyParams& Prop)
		: Super(InOwner, reinterpret_cast<const UECodeGen_Private::FPropertyParamsBaseWithOffset&>(Prop))
	{
	}

	TFVersePropertyBase(FFieldVariant InOwner, const UECodeGen_Private::FClassPropertyParams& Prop)
		: Super(InOwner, reinterpret_cast<const UECodeGen_Private::FPropertyParamsBaseWithOffset&>(Prop))
	{
	}

#if UE_WITH_CONSTINIT_UOBJECT
	explicit consteval TFVersePropertyBase(UE::CodeGen::ConstInit::FPropertyParams InBaseParams)
		: Super(InBaseParams)
	{
	}
#endif

#if WITH_EDITORONLY_DATA
	explicit TFVersePropertyBase(UField* InField)
		: Super(InField)
	{
	}
#endif // WITH_EDITORONLY_DATA

	// UHT interface
	COREUOBJECT_API virtual FString GetCPPMacroType(FString& ExtendedTypeText) const override;
	// End of UHT interface

	// FProperty interface
	COREUOBJECT_API virtual bool Identical(const void* A, const void* B, uint32 PortFlags) const override;
	COREUOBJECT_API virtual void SerializeItem(FStructuredArchive::FSlot Slot, void* Value, void const* Defaults) const override;
	COREUOBJECT_API virtual void ExportText_Internal(FString& ValueStr, const void* PropertyValueOrContainer, EPropertyPointerType PointerType, const void* DefaultValue, UObject* Parent, int32 PortFlags, UObject* ExportRootScope) const override;
	COREUOBJECT_API virtual const TCHAR* ImportText_Internal(const TCHAR* Buffer, void* ContainerOrPropertyPtr, EPropertyPointerType PropertyPointerType, UObject* OwnerObject, int32 PortFlags, FOutputDevice* ErrorText) const override;
	COREUOBJECT_API virtual bool ContainsObjectReference(TArray<const FStructProperty*>& EncounteredStructProps, EPropertyObjectReferenceType InReferenceType = EPropertyObjectReferenceType::Strong) const override;
	COREUOBJECT_API virtual void EmitReferenceInfo(UE::GC::FSchemaBuilder& Schema, int32 BaseOffset, TArray<const FStructProperty*>& EncounteredStructProps, UE::GC::FPropertyStack& DebugPath) override;

	virtual bool HasIntrusiveUnsetOptionalState() const override
	{
		return false;
	}
	// End of FProperty interface
};

class FVCellProperty : public TFVersePropertyBase<Verse::TWriteBarrier<Verse::VCell>>
{
	DECLARE_FIELD_API(FVCellProperty, TFVersePropertyBase<Verse::TWriteBarrier<Verse::VCell>>, CASTCLASS_FVCellProperty, COREUOBJECT_API)

public:
	COREUOBJECT_API FVCellProperty(FFieldVariant InOwner, const FName& InName, EObjectFlags InObjectFlags);

	/**
	 * Constructor used for constructing compiled in properties
	 * @param InOwner Owner of the property
	 * @param Prop Pointer to the compiled in structure describing the property
	 **/
	COREUOBJECT_API FVCellProperty(FFieldVariant InOwner, const UECodeGen_Private::FVerseValuePropertyParams& Prop);

#if UE_WITH_CONSTINIT_UOBJECT
	explicit consteval FVCellProperty(UE::CodeGen::ConstInit::FPropertyParams InBaseParams)
		: Super(InBaseParams)
	{
	}
#endif
};

//
// Metadata for a property of FVValueProperty type.
//
class FVValueProperty : public TFVersePropertyBase<Verse::TWriteBarrier<Verse::VValue>>
{
	DECLARE_FIELD_API(FVValueProperty, TFVersePropertyBase<Verse::TWriteBarrier<Verse::VValue>>, CASTCLASS_FVValueProperty, COREUOBJECT_API)

public:
	COREUOBJECT_API FVValueProperty(FFieldVariant InOwner, const FName& InName, EObjectFlags InObjectFlags);

	/**
	 * Constructor used for constructing compiled in properties
	 * @param InOwner Owner of the property
	 * @param Prop Pointer to the compiled in structure describing the property
	 **/
	COREUOBJECT_API FVValueProperty(FFieldVariant InOwner, const UECodeGen_Private::FVerseValuePropertyParams& Prop);
	
#if UE_WITH_CONSTINIT_UOBJECT
	explicit consteval FVValueProperty(UE::CodeGen::ConstInit::FPropertyParams InBaseParams)
		: Super(InBaseParams)
	{
	}
#endif
};

//
// Metadata for a property of FVRestValueProperty type.
//
class FVRestValueProperty : public TFVersePropertyBase<Verse::VRestValue>
{
	DECLARE_FIELD_API(FVRestValueProperty, TFVersePropertyBase<Verse::VRestValue>, CASTCLASS_FVRestValueProperty, COREUOBJECT_API)

public:

	Verse::TWriteBarrier<Verse::VValue> Type;
	FProperty* LegacyProperty = nullptr;
#if WITH_EDITORONLY_DATA
	// See CDataDefinition::IsNativeRepresentation for the meaning of this flag.
	bool bNativeRepresentation = true;
#endif

	COREUOBJECT_API FVRestValueProperty(FFieldVariant InOwner, const FName& InName, EObjectFlags InObjectFlags);

	/**
	 * Constructor used for constructing compiled in properties
	 * @param InOwner Owner of the property
	 * @param Prop Pointer to the compiled in structure describing the property
	 **/
	COREUOBJECT_API FVRestValueProperty(FFieldVariant InOwner, const UECodeGen_Private::FVerseValuePropertyParams& Prop);
	COREUOBJECT_API FVRestValueProperty(FFieldVariant InOwner, const UECodeGen_Private::FVerseClassPropertyParams& Prop);

#if UE_WITH_CONSTINIT_UOBJECT
	explicit consteval FVRestValueProperty(UE::CodeGen::ConstInit::FPropertyParams InBaseParams)
		: Super(InBaseParams)
	{
	}
#endif

	COREUOBJECT_API virtual ~FVRestValueProperty();
	COREUOBJECT_API virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	COREUOBJECT_API FProperty* GetOrCreateLegacyProperty(Verse::FAllocationContext Context);

	COREUOBJECT_API virtual void PostDuplicate(const FField& InField) override;

#if WITH_EDITORONLY_DATA
	// For conversion from uncooked BPVM data during deserialization.
	COREUOBJECT_API static Verse::VValue ConvertToVValueLegacy(Verse::FAllocationContext Context, const void* Container, FProperty* Property);
	COREUOBJECT_API static bool ConvertFromVValueLegacy(Verse::FAllocationContext Context, void* Container, FProperty* Property, Verse::VValue Value);
	COREUOBJECT_API virtual EConvertFromTypeResult ConvertFromType(const FPropertyTag& Tag, FStructuredArchive::FSlot Slot, uint8* Data, const UStruct* DefaultsStruct, const uint8* Defaults) override;
#endif

	COREUOBJECT_API virtual void CopyValuesInternal(void* Dest, const void* Src, int32 Count) const override;
	COREUOBJECT_API virtual void InstanceSubobjects(void* Data, const void* DefaultData, TNotNull<UObject*> Owner, FObjectInstancingGraph* InstanceGraph) override;
};

inline bool IsVerseProperty(const FProperty* Property)
{
	return Property->IsA<FVRestValueProperty>() || Property->IsA<FVValueProperty>() || Property->IsA<FVCellProperty>();
}

COREUOBJECT_API TSharedPtr<FJsonValue> VersePropertyToJSON(Verse::FRunningContext Context, const FProperty* Property, const void* InValue, TMap<const void*, Verse::EVisitState>& VisitedObjects, const Verse::VerseVMToJsonCallback& Callback, const uint32 RecursionDepth = 0);

#endif // WITH_VERSE_VM

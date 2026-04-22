// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/UnrealType.h"

#define UE_API COREUOBJECT_API

// FVerseClassProperty
class FVerseClassProperty : public FClassProperty
{
	DECLARE_FIELD_API(FVerseClassProperty, FClassProperty, CASTCLASS_None, UE_API)
	
	bool bRequiresConcrete = false;
	bool bRequiresCastable = false;

public:
	typedef FVerseClassProperty::TTypeFundamentals TTypeFundamentals;
	typedef TTypeFundamentals::TCppType TCppType;

public:
	UE_API FVerseClassProperty(FFieldVariant InOwner, const FName& InName, EObjectFlags InObjectFlags);

	/**
	 * Constructor used for constructing compiled in properties
	 * @param InOwner Owner of the property
	 * @param PropBase Pointer to the compiled in structure describing the property
	 **/
	UE_API FVerseClassProperty(FFieldVariant InOwner, const UECodeGen_Private::FVerseClassPropertyParams& Prop);

	// UObject interface
	UE_API virtual void Serialize(FArchive& Ar) override;
	// End of UObject interface

	// UHT interface
	UE_API virtual FString GetCPPMacroType(FString& ExtendedTypeText) const override;
	// End of UHT interface

	// FProperty interface
	UE_API virtual const TCHAR* ImportText_Internal(const TCHAR* Buffer, void* ContainerOrPropertyPtr, EPropertyPointerType PropertyPointerType, UObject* OwnerObject, int32 PortFlags, FOutputDevice* ErrorText) const override;
	UE_API virtual bool SameType(const FProperty* Other) const override;
#if WITH_EDITORONLY_DATA
	UE_API virtual void AppendSchemaHash(FBlake3& Builder, bool bSkipEditorOnly) const override;
#endif
	// End of FProperty interface
};

#undef UE_API

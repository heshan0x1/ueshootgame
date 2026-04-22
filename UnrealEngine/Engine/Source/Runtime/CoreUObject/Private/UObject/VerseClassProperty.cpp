// Copyright Epic Games, Inc. All Rights Reserved.

#include "UObject/VerseClassProperty.h"

#include "CoreMinimal.h"
#include "Templates/Casts.h"

#include "UObject/PropertyTypeName.h"
#include "UObject/PropertyHelper.h"
#include "UObject/ObjectMacros.h"
#include "UObject/UnrealType.h"
#include "UObject/UnrealTypePrivate.h"
#include "VerseVM/VVMVerseClass.h"
#include "Hash/Blake3.h"

IMPLEMENT_FIELD(FVerseClassProperty)

FVerseClassProperty::FVerseClassProperty(FFieldVariant InOwner, const FName& InName, EObjectFlags InObjectFlags)
	: Super(InOwner, InName, InObjectFlags)
{
}

FVerseClassProperty::FVerseClassProperty(FFieldVariant InOwner, const UECodeGen_Private::FVerseClassPropertyParams& Prop)
	: Super{InOwner, (const UECodeGen_Private::FClassPropertyParams&)Prop}
	, bRequiresConcrete{Prop.bRequiresConcrete}
	, bRequiresCastable{Prop.bRequiresCastable}
{
}

void FVerseClassProperty::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);
	Ar << bRequiresConcrete;
	Ar << bRequiresCastable;
}

const TCHAR* FVerseClassProperty::ImportText_Internal(const TCHAR* Buffer, void* ContainerOrPropertyPtr, EPropertyPointerType PropertyPointerType, UObject* Parent, int32 PortFlags, FOutputDevice* ErrorText) const
{
	const TCHAR* Result = Super::ImportText_Internal(Buffer, ContainerOrPropertyPtr, PropertyPointerType, Parent, PortFlags, ErrorText);

	if (Result)
	{
		void* Data = PointerToValuePtr(ContainerOrPropertyPtr, PropertyPointerType);
		UObject* AssignedVersePropertyObject = GetObjectPropertyValue(Data);
		UVerseClass* AssignedVersePropertyClass = Cast<UVerseClass>(AssignedVersePropertyObject);
		// Validate constraints
		if ((bRequiresConcrete && (!AssignedVersePropertyClass || !AssignedVersePropertyClass->IsConcrete()))
			|| (bRequiresCastable && (!AssignedVersePropertyClass || !AssignedVersePropertyClass->IsExplicitlyCastable())))
		{
			ErrorText->Logf(TEXT("Invalid object '%s' specified for property '%s'"), *AssignedVersePropertyObject->GetFullName(), *GetName());
			UObject* NullObj = nullptr;

			if (PropertyPointerType == EPropertyPointerType::Container && HasSetter())
			{
				SetValue_InContainer(ContainerOrPropertyPtr, NullObj);
			}
			else
			{
				SetObjectPropertyValue(PointerToValuePtr(ContainerOrPropertyPtr, PropertyPointerType), NullObj);
			}
			Result = nullptr;
		}
	}

	return Result;
}

FString FVerseClassProperty::GetCPPMacroType(FString& ExtendedTypeText) const
{
	if (PropertyFlags & CPF_TObjectPtr)
	{
		ExtendedTypeText = FString::Printf(TEXT("TObjectPtr<%s%s>"), PropertyClass->GetPrefixCPP(), *PropertyClass->GetName());
		return TEXT("OBJECTPTR");
	}
	ExtendedTypeText = TEXT("UVerseClass");
	return TEXT("OBJECT");
}

bool FVerseClassProperty::SameType(const FProperty* Other) const
{
	return Super::SameType(Other)
		&& (bRequiresConcrete == static_cast<const FVerseClassProperty*>(Other)->bRequiresConcrete)
		&& (bRequiresCastable == static_cast<const FVerseClassProperty*>(Other)->bRequiresCastable);
}

#if WITH_EDITORONLY_DATA
void FVerseClassProperty::AppendSchemaHash(FBlake3& Builder, bool bSkipEditorOnly) const
{
	Super::AppendSchemaHash(Builder, bSkipEditorOnly);

	uint8_t RequiresConcrete = bRequiresConcrete;
	Builder.Update(&RequiresConcrete, 1);
	uint8_t RequiresCastable = bRequiresCastable;
	Builder.Update(&RequiresCastable, 1);
}
#endif

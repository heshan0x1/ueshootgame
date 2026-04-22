// Copyright Epic Games, Inc. All Rights Reserved.

#include "Variables/AnimNextVariableReference.h"
#include "AnimNextRigVMAsset.h"
#include "Variables/AnimNextSoftVariableReference.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AnimNextVariableReference)

FAnimNextVariableReference::FAnimNextVariableReference(const FAnimNextSoftVariableReference& InSoftReference)
	: Name(InSoftReference.GetName())
	, Object(InSoftReference.GetSoftObjectPath().TryLoad())
{
	check(Object == nullptr || Object->IsA<UAnimNextRigVMAsset>() || Object->IsA<UScriptStruct>());
}

FAnimNextVariableReference::FAnimNextVariableReference(FName InName, const UAnimNextRigVMAsset* InAsset)
	: Name(InName)
	, Object(InAsset)
{
	check(InAsset != nullptr);
}

FAnimNextVariableReference::FAnimNextVariableReference(FName InName, const UScriptStruct* InStruct)
	: Name(InName)
	, Object(InStruct)
{
	check(InStruct != nullptr);
}

bool FAnimNextVariableReference::IsValid() const
{
	// TODO: Remove legacy name-based lookup path when deprecations are all fixed up
	if (Object == nullptr)
	{
		return !Name.IsNone();
	}

	return ResolveProperty() != nullptr;
}

const FProperty* FAnimNextVariableReference::ResolveProperty() const
{
	if (CachedProperty.IsPathToFieldEmpty() || CachedProperty.IsStale())
	{
		if (const UAnimNextRigVMAsset* Asset = Cast<UAnimNextRigVMAsset>(Object))
		{
			if (const FPropertyBagPropertyDesc* Desc = Asset->GetVariableDefaults().FindPropertyDescByName(Name))
			{
				CachedProperty = Desc->CachedProperty;
			}
		}
		else if (const UScriptStruct* Struct = Cast<UScriptStruct>(Object))
		{
			if (const FProperty* FoundProperty = Struct->FindPropertyByName(Name))
			{
				CachedProperty = FoundProperty;
			}
		}
	}
	return CachedProperty.Get();
}

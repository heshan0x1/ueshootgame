// Copyright Epic Games, Inc. All Rights Reserved.

#include "Variables/AnimNextSoftVariableReference.h"
#include "AnimNextRigVMAsset.h"
#include "Variables/AnimNextVariableReference.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AnimNextSoftVariableReference)

FAnimNextSoftVariableReference::FAnimNextSoftVariableReference(const FAnimNextVariableReference& InVariableReference)
	: Name(InVariableReference.GetName())
	, SoftObjectPath(InVariableReference.GetObject())
{
	const UObject* Object = InVariableReference.GetObject();
	check(Object == nullptr || Object->IsA<UAnimNextRigVMAsset>() || Object->IsA<UScriptStruct>());
}


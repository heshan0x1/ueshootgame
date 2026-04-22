// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimNextEntryAssetDefinitions.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AnimNextEntryAssetDefinitions)

#define LOCTEXT_NAMESPACE "AnimNextAssetDefinitions"

FText UAssetDefinition_AnimNextVariableEntry::GetObjectDisplayNameText(UObject* Object) const
{
	UAnimNextRigVMAssetEntry* Parameter = CastChecked<UAnimNextRigVMAssetEntry>(Object);
	return Parameter->GetDisplayName();
}

FText UAssetDefinition_AnimNextEventGraphEntry::GetObjectDisplayNameText(UObject* Object) const
{
	UAnimNextRigVMAssetEntry* Variable = CastChecked<UAnimNextRigVMAssetEntry>(Object);
	return Variable->GetDisplayName();
}

FText UAssetDefinition_AnimNextSharedVariablesEntry::GetObjectDisplayNameText(UObject* Object) const
{
	UAnimNextSharedVariablesEntry* SharedVariables = CastChecked<UAnimNextSharedVariablesEntry>(Object);
	return SharedVariables->GetDisplayName();
}

#undef LOCTEXT_NAMESPACE

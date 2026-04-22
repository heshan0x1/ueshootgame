// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimNextAnimGraphEntryAssetDefinitions.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AnimNextAnimGraphEntryAssetDefinitions)

#define LOCTEXT_NAMESPACE "AnimNextAnimGraphEntryAssetDefinitions"

FText UAssetDefinition_AnimNextAnimationGraphEntry::GetObjectDisplayNameText(UObject* Object) const
{
	UAnimNextRigVMAssetEntry* Parameter = CastChecked<UAnimNextRigVMAssetEntry>(Object);
	return Parameter->GetDisplayName();
}

#undef LOCTEXT_NAMESPACE

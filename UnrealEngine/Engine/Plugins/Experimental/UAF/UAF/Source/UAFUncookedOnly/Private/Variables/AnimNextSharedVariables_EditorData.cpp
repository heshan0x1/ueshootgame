// Copyright Epic Games, Inc. All Rights Reserved.

#include "Variables/AnimNextSharedVariables_EditorData.h"

#include "Entries/AnimNextSharedVariablesEntry.h"
#include "Entries/AnimNextVariableEntry.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AnimNextSharedVariables_EditorData)

TConstArrayView<TSubclassOf<UAnimNextRigVMAssetEntry>> UAnimNextSharedVariables_EditorData::GetEntryClasses() const
{
	static const TSubclassOf<UAnimNextRigVMAssetEntry> Classes[] =
	{
		UAnimNextVariableEntry::StaticClass(),
		UAnimNextSharedVariablesEntry::StaticClass(),
	};

	return Classes;
}

void UAnimNextSharedVariables_EditorData::CustomizeNewAssetEntry(UAnimNextRigVMAssetEntry* InNewEntry) const
{
	UAnimNextVariableEntry* VariableEntry = Cast<UAnimNextVariableEntry>(InNewEntry);
	if(VariableEntry == nullptr)
	{
		return;
	}
	
	const bool bIsSharedVariables = ExactCast<UAnimNextSharedVariables_EditorData>(this) != nullptr;
	if(!bIsSharedVariables)
	{
		return;
	}

	// Force all variables in 'pure' shared variables assets to be public
	VariableEntry->SetExportAccessSpecifier(EAnimNextExportAccessSpecifier::Public, false);
}

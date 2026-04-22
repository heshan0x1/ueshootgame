// Copyright Epic Games, Inc. All Rights Reserved.

#include "Entries/AnimNextCategoryEntry.h"
#include "AnimNextRigVMAssetEditorData.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AnimNextCategoryEntry)

void UAnimNextCategoryEntry::Initialize(UAnimNextRigVMAssetEditorData* InEditorData)
{
	Super::Initialize(InEditorData);
}

void UAnimNextCategoryEntry::SetEntryName(FName InName, bool bSetupUndoRedo)
{
	if(bSetupUndoRedo)
	{
		Modify();
	}

	CategoryName = InName;
	BroadcastModified(EAnimNextEditorDataNotifType::EntryRenamed);
}

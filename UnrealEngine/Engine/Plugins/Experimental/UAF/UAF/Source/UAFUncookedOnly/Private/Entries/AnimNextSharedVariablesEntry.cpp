// Copyright Epic Games, Inc. All Rights Reserved.

#include "Entries/AnimNextSharedVariablesEntry.h"

#if WITH_LIVE_CODING
#include "ILiveCodingModule.h"
#endif
#include "UncookedOnlyUtils.h"
#include "Variables/AnimNextSharedVariables_EditorData.h"
#include "Entries/AnimNextVariableEntry.h"
#include "Logging/StructuredLog.h"
#include "Modules/ModuleManager.h"
#include "Param/ParamType.h"
#include "Variables/AnimNextSharedVariables.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AnimNextSharedVariablesEntry)

#define LOCTEXT_NAMESPACE "AnimNextSharedVariablesEntry"

void UAnimNextSharedVariablesEntry::Initialize(UAnimNextRigVMAssetEditorData* InEditorData)
{
	Super::Initialize(InEditorData);

	if(Asset)
	{
		UAnimNextSharedVariables_EditorData* EditorData = UE::UAF::UncookedOnly::FUtils::GetEditorData<UAnimNextSharedVariables_EditorData>(Asset.Get());
		EditorData->ModifiedDelegate.AddUObject(this, &UAnimNextSharedVariablesEntry::HandleAssetModified);
	}
	else if (Struct)
	{
#if WITH_LIVE_CODING
		if (ILiveCodingModule* LiveCoding = FModuleManager::LoadModulePtr<ILiveCodingModule>(LIVE_CODING_MODULE_NAME))
		{
			LiveCoding->GetOnPatchCompleteDelegate().AddUObject(this, &UAnimNextSharedVariablesEntry::HandlePatchComplete);
		}
#endif
	}
}

FName UAnimNextSharedVariablesEntry::GetEntryName() const
{
	switch (Type)
	{
	case EAnimNextSharedVariablesType::Asset:
		return Asset ? Asset->GetFName() : NAME_None;
	case EAnimNextSharedVariablesType::Struct:
		return Struct ? Struct->GetFName() : NAME_None;
	}
	
	return NAME_None;
}

FText UAnimNextSharedVariablesEntry::GetDisplayName() const
{
	switch (Type)
	{
	case EAnimNextSharedVariablesType::Asset:
		return Asset ? FText::FromName(Asset->GetFName()) : LOCTEXT("InvalidSharedVariables", "Invalid Shared Variables");
	case EAnimNextSharedVariablesType::Struct:
		return Struct ? Struct->GetDisplayNameText() : LOCTEXT("InvalidSharedVariables", "Invalid Shared Variables");
	}

	return FText::GetEmpty();
}

FText UAnimNextSharedVariablesEntry::GetDisplayNameTooltip() const
{
	switch (Type)
	{
	case EAnimNextSharedVariablesType::Asset:
		return Asset ? FText::FromName(Asset->GetFName()) : LOCTEXT("InvalidSharedVariablesTooltip", "Invalid or deleted Shared Variables");
	case EAnimNextSharedVariablesType::Struct:
		return Struct ? FText::FromString(Struct->GetPathName()) : LOCTEXT("InvalidSharedVariables", "Invalid Shared Variables");
	}

	return FText::GetEmpty();
}

void UAnimNextSharedVariablesEntry::SetAsset(const UAnimNextSharedVariables* InAsset, bool bSetupUndoRedo)
{
	check(InAsset != nullptr);

	if(bSetupUndoRedo)
	{
		Modify();
	}

	Type = EAnimNextSharedVariablesType::Asset;
	Asset = InAsset;
	ObjectPath = FSoftObjectPath(Asset);
	Struct = nullptr;
}

const UAnimNextSharedVariables* UAnimNextSharedVariablesEntry::GetAsset() const
{
	return Type == EAnimNextSharedVariablesType::Asset ? Asset : nullptr;
}

FSoftObjectPath UAnimNextSharedVariablesEntry::GetObjectPath() const
{
	return ObjectPath;
}

void UAnimNextSharedVariablesEntry::SetStruct(const UScriptStruct* InStruct, bool bSetupUndoRedo)
{
	check(InStruct != nullptr);

	if(bSetupUndoRedo)
	{
		Modify();
	}

	Type = EAnimNextSharedVariablesType::Struct;
	Struct = InStruct;
	ObjectPath = FSoftObjectPath(Struct);
	Asset = nullptr;
}

const UScriptStruct* UAnimNextSharedVariablesEntry::GetStruct() const
{
	return Type == EAnimNextSharedVariablesType::Struct ? Struct : nullptr;
}

void UAnimNextSharedVariablesEntry::HandleAssetModified(UAnimNextRigVMAssetEditorData* InEditorData, EAnimNextEditorDataNotifType InType, UObject* InSubject)
{
	switch(InType)
	{
	case EAnimNextEditorDataNotifType::UndoRedo:
	case EAnimNextEditorDataNotifType::EntryAdded:
	case EAnimNextEditorDataNotifType::EntryRemoved:
	case EAnimNextEditorDataNotifType::EntryRenamed:
	case EAnimNextEditorDataNotifType::EntryAccessSpecifierChanged:
	case EAnimNextEditorDataNotifType::VariableTypeChanged:
	case EAnimNextEditorDataNotifType::VariableDefaultValueChanged:
	case EAnimNextEditorDataNotifType::VariableBindingChanged:
		if(UAnimNextRigVMAssetEditorData* EditorData = GetTypedOuter<UAnimNextRigVMAssetEditorData>())
		{
			EditorData->RequestAutoVMRecompilation();
		}
		break;
	default:
		break;
	}
}

#if WITH_LIVE_CODING
void UAnimNextSharedVariablesEntry::HandlePatchComplete()
{
	if(UAnimNextRigVMAssetEditorData* EditorData = GetTypedOuter<UAnimNextRigVMAssetEditorData>())
	{
		EditorData->RequestAutoVMRecompilation();
	}
}
#endif

#undef LOCTEXT_NAMESPACE

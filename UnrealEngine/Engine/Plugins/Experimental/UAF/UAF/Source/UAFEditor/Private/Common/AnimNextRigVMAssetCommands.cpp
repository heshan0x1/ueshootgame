// Copyright Epic Games, Inc. All Rights Reserved.

#include "Common/AnimNextRigVMAssetCommands.h"

#define LOCTEXT_NAMESPACE "AnimNextRigVMAssetCommands"

namespace UE::UAF
{

FAnimNextRigVMAssetCommands::FAnimNextRigVMAssetCommands()
	: TCommands<FAnimNextRigVMAssetCommands>("UAFRigVMAssetCommand", LOCTEXT("UAFRigVMAssetCommands", "UAF Asset Commands"), NAME_None, "AnimNextStyle")
{
}

void FAnimNextRigVMAssetCommands::RegisterCommands()
{
	UI_COMMAND(FindInAnimNextRigVMAsset, "FindInUAFRigVMAsset", "Search the current UAF Asset.", EUserInterfaceActionType::Button, FInputChord(EModifierKey::Control, EKeys::F));
	UI_COMMAND(FindAndReplaceInAnimNextRigVMAsset, "FindAndReplaceInUAFRigVMAsset", "Find and Replace across UAF Assets.", EUserInterfaceActionType::Button, FInputChord(EModifierKey::Control | EModifierKey::Shift, EKeys::F));
}

}

#undef LOCTEXT_NAMESPACE

// Copyright Epic Games, Inc. All Rights Reserved.

#include "Editor/ModularRigEventQueueCommands.h"

#define LOCTEXT_NAMESPACE "ModularRigEventQueueCommands"

void FModularRigEventQueueCommands::RegisterCommands()
{
	UI_COMMAND(FocusOnSelection, "Focus On Selection", "Finds the selected event's module in the modular rig tree.", EUserInterfaceActionType::Button, FInputChord());
}

#undef LOCTEXT_NAMESPACE

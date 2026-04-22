// Copyright Epic Games, Inc. All Rights Reserved.


#include "VariablesOutlinerCommands.h"

#define LOCTEXT_NAMESPACE "VariablesOutlinerCommands"

void FVariablesOutlinerCommands::RegisterCommands()
{
	UI_COMMAND(AddNewVariable, "Add Variable", "Adds a single new variable to the selected asset.", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(AddNewVariables, "Add Variable(s)", "Adds variables to assets.\nIf multiple assets are selected, then variables will be added to each.\nIf no assets are selected and there are multiple assets, variables will be added to all assets.", EUserInterfaceActionType::Button, FInputChord());

	UI_COMMAND(FindReferences, "In Project", "Finds all references to the selected variable variables across assets in the project.", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(FindReferencesInWorkspace, "In Workspace", "Finds all references to the selected variable variables across assets in the current workspace.", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(FindReferencesInAsset, "In Asset", "Finds all references to the selected variable variables in the currently opened asset.", EUserInterfaceActionType::Button, FInputChord());

	UI_COMMAND(SaveAsset, "Save Asset", "Save the selected asset.", EUserInterfaceActionType::Button, FInputChord());

	UI_COMMAND(ToggleVariableExport, "Toggle Variable Export", "Sets Variable to be either Public or Private.", EUserInterfaceActionType::Button, FInputChord());

	UI_COMMAND(CreateSharedVariablesAssets, "Create Shared Variables from Selection", "Creates a new Shared Variables asset from the selected set of variables.", EUserInterfaceActionType::Button, FInputChord());
}

#undef LOCTEXT_NAMESPACE
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Framework/Commands/Commands.h"
#include "Styling/AppStyle.h"

#define LOCTEXT_NAMESPACE "VariablesOutlinerCommands"

class FVariablesOutlinerCommands : public TCommands<FVariablesOutlinerCommands>
{
public:

	FVariablesOutlinerCommands() : TCommands<FVariablesOutlinerCommands>( TEXT("VariablesOutliner"), LOCTEXT("VariablesOutlinerCommands", "Variables Outliner Commands"), NAME_None, FAppStyle::GetAppStyleSetName() )
	{
	}
	
	virtual void RegisterCommands() override;

	TSharedPtr<FUICommandInfo> AddNewVariable;
	TSharedPtr<FUICommandInfo> AddNewVariables;
	TSharedPtr<FUICommandInfo> FindReferences;
	TSharedPtr<FUICommandInfo> FindReferencesInWorkspace;
	TSharedPtr<FUICommandInfo> FindReferencesInAsset;
	TSharedPtr<FUICommandInfo> SaveAsset;	
	TSharedPtr<FUICommandInfo> ToggleVariableExport;
	TSharedPtr<FUICommandInfo> CreateSharedVariablesAssets;
};

#undef LOCTEXT_NAMESPACE
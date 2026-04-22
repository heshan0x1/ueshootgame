// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Framework/Commands/Commands.h"

class FPVEditorCommands : public TCommands<FPVEditorCommands>
{
public:
	FPVEditorCommands();

	// ~Begin TCommands<> interface
	virtual void RegisterCommands() override;
	// ~End TCommands<> interface

	//Editor Commands
	TSharedPtr<FUICommandInfo> Export;

	//Viewport Commands
	TSharedPtr<FUICommandInfo> ShowMannequin;
	TSharedPtr<FUICommandInfo> ShowScaleVisualization;
	TSharedPtr<FUICommandInfo> AutoFocusViewport;
	TSharedPtr<FUICommandInfo> LockNodeInspection;;
};
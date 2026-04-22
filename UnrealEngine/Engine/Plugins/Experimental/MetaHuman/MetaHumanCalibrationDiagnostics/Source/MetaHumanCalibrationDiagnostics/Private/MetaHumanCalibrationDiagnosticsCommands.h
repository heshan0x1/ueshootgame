// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Framework/Commands/Commands.h"
#include "Framework/Commands/InputChord.h"

#include "Styling/AppStyle.h"

#define LOCTEXT_NAMESPACE "MetaHumanCalibrationDiagnostics"

class FMetaHumanCalibrationDiagnosticsCommands
	: public TCommands<FMetaHumanCalibrationDiagnosticsCommands>
{
public:

	/** Default constructor. */
	FMetaHumanCalibrationDiagnosticsCommands()
		: TCommands<FMetaHumanCalibrationDiagnosticsCommands>(
			"MetaHumanCalibrationDiagnostics",
			NSLOCTEXT("Contexts", "MetaHumanCalibrationDiagnostics", "MetaHuman Calibration Diagnostics"),
			NAME_None, FAppStyle::GetAppStyleSetName()
		)
	{
	}

public:

	//~ TCommands interface
	virtual void RegisterCommands() override
	{
		UI_COMMAND(SelectAreaOfInterest, "Select Area of Interest", "Selects area of interest for a view", EUserInterfaceActionType::Button, FInputChord());
		UI_COMMAND(ResetView, "Reset View", "Resets the view to default", EUserInterfaceActionType::Button, FInputChord(EKeys::F));
		UI_COMMAND(TogglePoints, "Detected Points", "Shows/hides the detected points overlay", EUserInterfaceActionType::ToggleButton, FInputChord(EKeys::P));
		UI_COMMAND(TogglePerBlockErrors, "Per Block Errors", "Shows/hides the errors within a block. The image is split in 4x6 blocks.", EUserInterfaceActionType::ToggleButton, FInputChord(EKeys::E));
		UI_COMMAND(ToggleAreaOfInterest, "Area of Interest", "Shows/hides the current area of interest.", EUserInterfaceActionType::ToggleButton, FInputChord());
	}

public:
	/** Enters area of interest selection mode. */
	TSharedPtr<FUICommandInfo> SelectAreaOfInterest;

	/** Resets view. */
	TSharedPtr<FUICommandInfo> ResetView;

	/** Shows detected points */
	TSharedPtr<FUICommandInfo> TogglePoints;

	/** Shows detected points */
	TSharedPtr<FUICommandInfo> TogglePerBlockErrors;

	/** Shows Area of Interest */
	TSharedPtr<FUICommandInfo> ToggleAreaOfInterest;
};

#undef LOCTEXT_NAMESPACE
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Framework/Commands/Commands.h"

UE_DECLARE_TCOMMANDS(class FMetaHumanToolkitCommands, METAHUMANTOOLKIT_API)

/** Commands used by the base MetaHuman tookit */
class FMetaHumanToolkitCommands
	: public TCommands<FMetaHumanToolkitCommands>
{
public:
	 METAHUMANTOOLKIT_API FMetaHumanToolkitCommands();

	//~Begin TCommands<> interface
	METAHUMANTOOLKIT_API virtual void RegisterCommands() override;
	//~End TCommands<> interface

public:
	TSharedPtr<FUICommandInfo> ViewMixToSingle;
	TSharedPtr<FUICommandInfo> ViewMixToWipe;
	TSharedPtr<FUICommandInfo> ViewMixToDual;

	TSharedPtr<FUICommandInfo> ToggleSingleViewToA;
	TSharedPtr<FUICommandInfo> ToggleSingleViewToB;

	TSharedPtr<FUICommandInfo> ToggleRGBChannel;
	TSharedPtr<FUICommandInfo> ToggleCurves;
	TSharedPtr<FUICommandInfo> ToggleControlVertices;
	TSharedPtr<FUICommandInfo> ToggleDepthMesh;
	TSharedPtr<FUICommandInfo> ToggleUndistortion;
};
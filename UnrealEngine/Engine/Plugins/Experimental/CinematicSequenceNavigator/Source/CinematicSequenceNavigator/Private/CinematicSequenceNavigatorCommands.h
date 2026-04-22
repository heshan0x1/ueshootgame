// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Framework/Commands/Commands.h"
#include "Styling/AppStyle.h"

class FCinematicSequenceNavigatorCommands : public TCommands<FCinematicSequenceNavigatorCommands>
{
public:
	FCinematicSequenceNavigatorCommands()
		: TCommands<FCinematicSequenceNavigatorCommands>(TEXT("CinematicSequenceNavigatorCommands")
		, NSLOCTEXT("CinematicSequenceNavigatorCommands"
			, "CinematicSequenceNavigatorCommands", "Cinematic Sequence Navigator Commands")
		, NAME_None
		, FAppStyle::GetAppStyleSetName())
	{
	}

	virtual void RegisterCommands() override;

	// @TODO: Add commands here
};

// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/Commands/Commands.h"

class CONTROLRIGEDITOR_API FModularRigEventQueueCommands : public TCommands<FModularRigEventQueueCommands>
{
public:
	FModularRigEventQueueCommands() : TCommands<FModularRigEventQueueCommands>
	(
		"FModularRigEventQueue",
		NSLOCTEXT("Contexts", "ModularRig", "Event Queue"),
		NAME_None, // "MainFrame" // @todo Fix this crash
		"ControlRigEditorStyle" // Icon Style Set
	)
	{}
	
	/** Focuses on a selected event's module */
	TSharedPtr< FUICommandInfo > FocusOnSelection;

	/**
	 * Initialize commands
	 */
	virtual void RegisterCommands() override;
};

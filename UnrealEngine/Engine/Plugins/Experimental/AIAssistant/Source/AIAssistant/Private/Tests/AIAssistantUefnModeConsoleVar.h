// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Misc/Optional.h"

#include "AIAssistantConsole.h"

class IConsoleVariable;

namespace UE::AIAssistant
{
	// Find the console variable that configures the UE/UEFN mode.
	IConsoleVariable* FindUefnModeConsoleVariable();

	// Get whether UEFN mode is enabled.
	bool IsUefnMode();

	// Wait long enough for an editor tick to notify of console variable modifications.
	constexpr float ConsoleVariableUpdateDelayInSeconds = 0.2f;

	// Saves the UEFN mode console variable on construction and restores it on destruction.
	class ScopedUefnModeConsoleVariableRestorer
	{
	public:
		ScopedUefnModeConsoleVariableRestorer();
		~ScopedUefnModeConsoleVariableRestorer();

	private:
		TOptional<bool> bPreviousValue;
	};
}
// Copyright Epic Games, Inc. All Rights Reserved.

#include "AIAssistantUefnModeConsoleVar.h"

#include "HAL/IConsoleManager.h"

namespace UE::AIAssistant
{
	IConsoleVariable* FindUefnModeConsoleVariable()
	{
		return IConsoleManager::Get().FindConsoleVariable(*UefnModeConsoleVariableName);
	}

	bool IsUefnMode()
	{
		auto* UefnModeConsoleVariable = FindUefnModeConsoleVariable();
		return UefnModeConsoleVariable ? UefnModeConsoleVariable->GetBool() : false;
	}

	ScopedUefnModeConsoleVariableRestorer::ScopedUefnModeConsoleVariableRestorer()
	{
		auto* UefnModeConsoleVariable = FindUefnModeConsoleVariable();
		if (UefnModeConsoleVariable)
		{
			bPreviousValue.Emplace(UefnModeConsoleVariable->GetBool());
		}
	}

	ScopedUefnModeConsoleVariableRestorer::~ScopedUefnModeConsoleVariableRestorer()
	{
		if (bPreviousValue.IsSet())
		{
			FindUefnModeConsoleVariable()->Set(bPreviousValue.GetValue());
		}
	}
}
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Framework/Commands/Commands.h"

class FAIAssistantCommands : public TCommands<FAIAssistantCommands>
{
public:
	FAIAssistantCommands();	

	// TCommands<> interface
	virtual void RegisterCommands() override;

	// IMPORTANT: The variable name of a command installed in a menu ends up being used as the menu
	// item name, for example this is installed by AIAssistant.cpp as
	// "LevelEditor.MainMenu.Window.OpenAIAssistantTab".

	TSharedPtr<FUICommandInfo> OpenAIAssistantTab;
	TSharedPtr<FUICommandInfo> SummonAIAssistantTab;
	TSharedPtr<FUICommandInfo> AISlateQueryCommand;
};
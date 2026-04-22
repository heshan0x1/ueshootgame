// Copyright Epic Games, Inc. All Rights Reserved.
#include "AudioModulationEditorCommands.h"

#include "AudioInsightsStyle.h"
#include "Framework/Commands/InputChord.h"
#include "Framework/Commands/UICommandInfo.h"

#define LOCTEXT_NAMESPACE "AudioModulationInsights"

namespace AudioModulationEditor
{
	FAudioModulationEditorCommands::FAudioModulationEditorCommands()
		: TCommands<FAudioModulationEditorCommands>("AudioModulationEditorCommands", LOCTEXT("AudioModulationEditorCommands_ContextDescText", "Audio Modulation Editor Commands"), NAME_None, UE::Audio::Insights::FSlateStyle::GetStyleName())
	{
	}
	
	void FAudioModulationEditorCommands::RegisterCommands()
	{
		UI_COMMAND(Browse, "Browse To Asset", "Browses to the selected sound asset in the content browser.", EUserInterfaceActionType::Button, FInputChord(EKeys::B, EModifierKey::Control));
		UI_COMMAND(Edit, "Edit", "Opens the selected sound for edit.", EUserInterfaceActionType::Button, FInputChord(EKeys::E, EModifierKey::Control));
	}
} // namespace AudioModulationEditor

#undef LOCTEXT_NAMESPACE

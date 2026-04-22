// Copyright Epic Games, Inc. All Rights Reserved.
#include "AudioEventLogEditorCommands.h"

#include "AudioInsightsStyle.h"
#include "Framework/Commands/InputChord.h"
#include "Framework/Commands/UICommandInfo.h"

#define LOCTEXT_NAMESPACE "AudioInsights"

namespace UE::Audio::Insights
{
	FAudioEventLogEditorCommands::FAudioEventLogEditorCommands()
		: TCommands<FAudioEventLogEditorCommands>("AudioEventLogEditorCommands", LOCTEXT("AudioEventLogEditorCommands_ContextDescText", "Audio Event Log Editor Commands"), NAME_None, UE::Audio::Insights::FSlateStyle::GetStyleName())
	{
	}

	void FAudioEventLogEditorCommands::RegisterCommands()
	{
		UI_COMMAND(Browse, "Browse To Asset", "Browses to the selected sound asset in the content browser.", EUserInterfaceActionType::Button, FInputChord(EKeys::B, EModifierKey::Control));
		UI_COMMAND(Edit, "Edit", "Opens the selected sound for edit.", EUserInterfaceActionType::Button, FInputChord(EKeys::E, EModifierKey::Control));
		UI_COMMAND(ResetInspectTimestampEventLog, "Reset Inspect Timestamp Event Log", "Resumes gathering the latest audio data and auto-scrolling in the Event Log if previously paused.", EUserInterfaceActionType::Button, FInputChord(EKeys::Escape));

		UI_COMMAND(AutoStopCachingWhenLastInCache, "When marked for deletion", "Automatically stops caching new data when inspecting an event that has been marked for deletion from the cache.", EUserInterfaceActionType::RadioButton, FInputChord(EKeys::F1, EModifierKey::Control));
		UI_COMMAND(AutoStopCachingOnInspect, "On inspect", "Automatically stops caching new data when inspecting an event.", EUserInterfaceActionType::RadioButton, FInputChord(EKeys::F2, EModifierKey::Control));
		UI_COMMAND(AutoStopCachingDisabled, "Never", "Disable automatically stopping the cache. Inspected events may be deleted from the cache during inspection.", EUserInterfaceActionType::RadioButton, FInputChord(EKeys::F3, EModifierKey::Control));
	}

} // namespace UE::Audio::Insights

#undef LOCTEXT_NAMESPACE

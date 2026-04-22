// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosVDSceneQueryVisualizationSettings.h"

#include "ChaosVDSettingsManager.h"
#include "Utils/ChaosVDUserInterfaceUtils.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(ChaosVDSceneQueryVisualizationSettings)


void UChaosVDSceneQueriesVisualizationSettings::SetDataVisualizationFlags(EChaosVDSceneQueryVisualizationFlags NewFlags)
{
	if (UChaosVDSceneQueriesVisualizationSettings* Settings = FChaosVDSettingsManager::Get().GetSettingsObject<UChaosVDSceneQueriesVisualizationSettings>())
	{
		Settings->GlobalSceneQueriesVisualizationFlags = static_cast<uint32>(NewFlags);
		Settings->BroadcastSettingsChanged();
	}
}

EChaosVDSceneQueryVisualizationFlags UChaosVDSceneQueriesVisualizationSettings::GetDataVisualizationFlags()
{
	if (UChaosVDSceneQueriesVisualizationSettings* Settings = FChaosVDSettingsManager::Get().GetSettingsObject<UChaosVDSceneQueriesVisualizationSettings>())
	{
		return static_cast<EChaosVDSceneQueryVisualizationFlags>(Settings->GlobalSceneQueriesVisualizationFlags);
	}

	return EChaosVDSceneQueryVisualizationFlags::None;
}

bool UChaosVDSceneQueriesVisualizationSettings::CanVisualizationFlagBeChangedByUI(uint32 Flag)
{
	return Chaos::VisualDebugger::Utils::ShouldVisFlagBeEnabledInUI(Flag, GlobalSceneQueriesVisualizationFlags, EChaosVDSceneQueryVisualizationFlags::EnableDraw);
}

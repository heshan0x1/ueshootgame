// Copyright Epic Games, Inc. All Rights Reserved.

#include "WaveformEditorTransformationsSettings.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(WaveformEditorTransformationsSettings)


FName UWaveformEditorTransformationsSettings::GetCategoryName() const
{
	return TEXT("Plugins");
}

#if WITH_EDITOR

FText UWaveformEditorTransformationsSettings::GetSectionText() const
{
	return NSLOCTEXT("WaveformEditorTransformations", "WaveformEditorTransformationsSettingsSection", "Waveform Editor Transformations");
}

FName UWaveformEditorTransformationsSettings::GetSectionName() const
{
	return TEXT("Waveform Editor Transformations");
}
#endif

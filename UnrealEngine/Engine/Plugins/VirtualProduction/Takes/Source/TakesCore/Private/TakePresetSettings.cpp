// Copyright Epic Games, Inc. All Rights Reserved.

#include "TakePresetSettings.h"

#include "LevelSequence.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(TakePresetSettings)

UTakePresetSettings::UTakePresetSettings()
	: TargetRecordClass(ULevelSequence::StaticClass())
{}

UTakePresetSettings* UTakePresetSettings::Get()
{
	return GetMutableDefault<UTakePresetSettings>();
}

UClass* UTakePresetSettings::GetTargetRecordClass() const
{
	return TargetRecordClass.TargetRecordClass ? TargetRecordClass.TargetRecordClass.Get() : ULevelSequence::StaticClass();
}

#if WITH_EDITOR
void UTakePresetSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	UObject::PostEditChangeProperty(PropertyChangedEvent);
	SaveConfig();
	OnSettingsChangedDelegate.Broadcast();
}
#endif

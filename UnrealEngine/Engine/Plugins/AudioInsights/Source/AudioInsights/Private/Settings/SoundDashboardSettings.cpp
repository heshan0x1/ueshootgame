// Copyright Epic Games, Inc. All Rights Reserved.
#include "Settings/SoundDashboardSettings.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(SoundDashboardSettings)

#if WITH_EDITOR

FSoundDashboardSettings::FOnReadSoundDashboardSettings FSoundDashboardSettings::OnReadSettings;
FSoundDashboardSettings::FOnWriteSoundDashboardSettings FSoundDashboardSettings::OnWriteSettings;
FSoundDashboardSettings::FOnRequestReadSoundDashboardSettings FSoundDashboardSettings::OnRequestReadSettings;
FSoundDashboardSettings::FOnRequestWriteSoundDashboardSettings FSoundDashboardSettings::OnRequestWriteSettings;

#endif // WITH_EDITOR

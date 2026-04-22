// Copyright Epic Games, Inc. All Rights Reserved.

#include "AudioMixerCVars.h"

#include "HAL/IConsoleManager.h"

namespace AudioMixerCVars
{
	int32 UseRenderScheduler = 0;
	FAutoConsoleVariableRef CVarUseAudioRenderScheduler(
		TEXT("au.UseRenderScheduler"),
		UseRenderScheduler,
		TEXT("Use audio rendering scheduler to handle render dependencies.\n")
		TEXT("0: Disabled, 1: Enabled"),
		ECVF_Default);
}

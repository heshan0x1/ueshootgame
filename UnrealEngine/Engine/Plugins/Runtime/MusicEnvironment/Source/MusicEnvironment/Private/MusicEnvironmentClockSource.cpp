// Copyright Epic Games, Inc. All Rights Reserved.

#include "MusicEnvironmentClockSource.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MusicEnvironmentClockSource)

void IMusicEnvironmentClockSource::GetCurrentBarBeat(float& Bar, float& BeatInBar)
{
	FMusicalTime MusicalTime = GetPositionMusicalTime();
	Bar = MusicalTime.FractionalBar();
	BeatInBar = MusicalTime.FractionalBeatInBar();
}

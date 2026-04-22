// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once 

struct FSubtitleAssetData;

#include "Engine/TimerHandle.h"
#include "Subtitles/SubtitlesAndClosedCaptionsDelegates.h"
#include "UObject/ObjectPtr.h"

#include "ActiveSubtitle.generated.h"

USTRUCT()
struct FActiveSubtitle
{
	GENERATED_BODY()

	UPROPERTY()
	FSubtitleAssetData Subtitle;

	FTimerHandle DurationTimerHandle;
};

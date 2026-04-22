// Copyright Epic Games, Inc. All Rights Reserved.

#include "Subtitles/SubtitlesAndClosedCaptionsDelegates.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(SubtitlesAndClosedCaptionsDelegates)

TDelegate<void(const FQueueSubtitleParameters&, const ESubtitleTiming)> FSubtitlesAndClosedCaptionsDelegates::QueueSubtitle;
TDelegate<bool(const FSubtitleAssetData&)> FSubtitlesAndClosedCaptionsDelegates::IsSubtitleActive;
TDelegate<void(const FSubtitleAssetData&)> FSubtitlesAndClosedCaptionsDelegates::StopSubtitle;
TDelegate<void()> FSubtitlesAndClosedCaptionsDelegates::StopAllSubtitles;

#if WITH_EDITORONLY_DATA
void USubtitleAssetUserData::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	for (FSubtitleAssetData& Subtitle : Subtitles)
	{
		Subtitle.bCanEditDuration = (Subtitle.SubtitleDurationType == ESubtitleDurationType::UseDurationProperty);
	}
}
#endif // WITH_EDITORONLY_DATA

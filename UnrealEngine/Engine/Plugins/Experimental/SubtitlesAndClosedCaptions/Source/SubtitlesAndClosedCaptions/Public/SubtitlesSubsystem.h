// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once 

#include "ActiveSubtitle.h"
#include "Fonts/SlateFontInfo.h"
#include "Math/Color.h"
#include "Subsystems/WorldSubsystem.h"

#include "SubtitleWidget.h"

#include "SubtitlesSubsystem.generated.h"

#define UE_API SUBTITLESANDCLOSEDCAPTIONS_API

class FCanvas;
class UAssetUserData;
class UFont;
struct FQueueSubtitleParameters;

#if WITH_DEV_AUTOMATION_TESTS
namespace SubtitlesAndClosedCaptions::Test
{
#if 0 // Temporarily disabling these tests as they have a dangling reference that trips up the static analysis on certain build configurations.
	struct FMovieSceneSubtitlesTest;
#endif
	struct FSubtitlesTest;
}
#endif

/*
* #SUBTITLES_PRD -	Requirement:	Ability to allow designers to “script” subtitle location for sequences and scenes to avoid subtitles overlapping important scenes or characters
*					Use a UEngineSubsystem for blueprints
*
*	Game configuration for font customization per game
*/
UCLASS(MinimalAPI, config = Game, defaultConfig)
class USubtitlesSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()
public:
	USubtitlesSubsystem() = default;

	// FSubtitlesAndClosedCaptionsDelegates

	// Adds a subtitle to the queue: Params contains the subtitle asset and an optional duration. The highest-priority subtitle in the queue will be displayed.
	// If Timing is ExternallyTimed, the queued subtitle will remain in the queue until manually removed.
	// If the subtitle asset has a non-zero StartOffset, it will sit in a delayed-start queue instead of being queued for display.
	UE_API virtual void QueueSubtitle(const FQueueSubtitleParameters& Params, const ESubtitleTiming Timing = ESubtitleTiming::InternallyTimed);

	// Returns true if the given subtitle asset is being displayed.
	UE_API virtual bool IsSubtitleActive(const FSubtitleAssetData& Data) const;

	// Stops the given subtitle asset being displayed.  This includes subtitles not yet being displayed due to their StartOffset
	UE_API virtual void StopSubtitle(const FSubtitleAssetData& Data);

	// Stops all queued subtitles from being displayed.  This includes subtitles not yet being displayed due to their StartOffset.
	UE_API virtual void StopAllSubtitles();

	UE_API virtual void ReplaceWidget(const TSubclassOf<USubtitleWidget>& NewWidgetAsset);

protected:

	UE_API virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	UE_API virtual void BindDelegates();

	UPROPERTY()
	TMap<ESubtitleType, FSlateFontInfo> SubtitleFontInfo;

	// Sorted by priority, desc
	UPROPERTY()
	TArray<FActiveSubtitle> ActiveSubtitles;

	// Unsorted; subtitles with a delayed start offset still need to be tracked before entering the queue proper.
	UPROPERTY()
	TArray<FActiveSubtitle> DelayedSubtitles;

protected:
	UE_API virtual void AddActiveSubtitle(const FSubtitleAssetData& Subtitle, float Duration, const float StartOffset, const ESubtitleTiming Timing);

	// These need to be UFUNCTIONs for the Timer Delegate bindings.
	UFUNCTION()
	UE_API virtual void MakeDelayedSubtitleActive(const FSubtitleAssetData& Subtitle, const ESubtitleTiming Timing);

	UFUNCTION()
	UE_API virtual void RemoveActiveSubtitle(const FSubtitleAssetData& Subtitle);

private:
	UE_API void AddAndDisplaySubtitle(FActiveSubtitle& NewActiveSubtitle);

#if WITH_DEV_AUTOMATION_TESTS
	// These functions are for testing subtitle functionality and aren't intended for general use.
public:
	// GetActiveSubtitles returns the sorted array of queued subtitles. Higher priorities are earlier in the array.
	// For equally-prioritized subtitles, the oldest one is first in this array.
	const TArray<FActiveSubtitle>& GetActiveSubtitles() const
	{
		return ActiveSubtitles;
	}

	// As this is for testing, assume that the number of subtitles has already been checked.
	const FSubtitleAssetData& GetTopRankedSubtitle() const
	{
		check(ActiveSubtitles.Num() > 0);
		const FActiveSubtitle& ActiveSubtitle = ActiveSubtitles[0];

		return ActiveSubtitle.Subtitle;
	}

	void TestActivatingDelayedSubtitle(const FSubtitleAssetData& Data)
	{
		MakeDelayedSubtitleActive(Data, ESubtitleTiming::InternallyTimed);
	}
#endif
	//	WITH_DEV_AUTOMATION_TESTS

private:
	UPROPERTY()
	TObjectPtr<USubtitleWidget> SubtitleWidget = nullptr;

	bool bInitializedWidget = false;
	UE_API bool TryCreateUMGWidgetFromAsset(const TSubclassOf<USubtitleWidget>& WidgetToUse);
	UE_API bool TryCreateUMGWidget();
	UE_API void UpdateWidgetData();
};

#undef UE_API

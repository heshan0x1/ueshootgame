// Copyright Epic Games, Inc. All Rights Reserved.

#include "SubtitlesSubsystem.h"

#include "Algo/Find.h"
#include "Algo/RemoveIf.h"
#include "CanvasItem.h"
#include "CanvasTypes.h"
#include "Containers/Ticker.h"
#include "Engine/Font.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Math/UnrealMathUtility.h"
#include "Styling/CoreStyle.h"
#include "SubtitlesAndClosedCaptionsModule.h"
#include "SubtitlesSettings.h"
#include "TimerManager.h"
#include "UnrealClient.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtrTemplates.h"
#include "UObject/WeakObjectPtrTemplates.h"

static constexpr float InfiniteDuration = FLT_MAX;

#include UE_INLINE_GENERATED_CPP_BY_NAME(SubtitlesSubsystem)

void USubtitlesSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	BindDelegates();
}

void USubtitlesSubsystem::BindDelegates()
{
	if (TryCreateUMGWidget())
	{
		FSubtitlesAndClosedCaptionsDelegates::QueueSubtitle.BindUObject(this, &USubtitlesSubsystem::QueueSubtitle);
		FSubtitlesAndClosedCaptionsDelegates::IsSubtitleActive.BindUObject(this, &USubtitlesSubsystem::IsSubtitleActive);
		FSubtitlesAndClosedCaptionsDelegates::StopSubtitle.BindUObject(this, &USubtitlesSubsystem::StopSubtitle);
		FSubtitlesAndClosedCaptionsDelegates::StopAllSubtitles.BindUObject(this, &USubtitlesSubsystem::StopAllSubtitles);
	}
}

void USubtitlesSubsystem::QueueSubtitle(const FQueueSubtitleParameters& Params, const ESubtitleTiming Timing)
{
	// Don't queue subtitles when the engine doesn't have them enabled or forced off. 
	if (GEngine == nullptr)
	{
		UE_LOG(LogSubtitlesAndClosedCaptions, Warning, TEXT("Couldn't check if subtitles are enabled, because GEngine nullptr."));
		return;
	}
	if (!GEngine->bSubtitlesEnabled || GEngine->bSubtitlesForcedOff)
	{
		return;
	}

	const FSubtitleAssetData& Subtitle = Params.Subtitle;
	UE_LOG(LogSubtitlesAndClosedCaptions, Display, TEXT("QueueSubtitle: '%s'"), *Subtitle.Text.ToString());


	// Externally-timed subtitles will be removed by the system queueing them, so they have an otherwise-infinite duration.
	float Duration = InfiniteDuration;
	if (Timing == ESubtitleTiming::InternallyTimed)
	{
		Duration = Params.Duration.IsSet() ? Params.Duration.GetValue() : Subtitle.Duration;
	}

	const float StartOffset = Params.StartOffset.IsSet() ? Params.StartOffset.GetValue() : Subtitle.StartOffset;

	if (IsInGameThread())
	{
		AddActiveSubtitle(Subtitle, Duration, StartOffset, Timing);
	}
	else
	{
		ExecuteOnGameThread(
			TEXT("USubtitlesSubsystem::HandleQueueSubtitle"),
			[WeakThis = TWeakObjectPtr<USubtitlesSubsystem>(this), Subtitle, Duration, StartOffset, Timing]()
			{
				if (TStrongObjectPtr<USubtitlesSubsystem> ThisPin = WeakThis.Pin())
				{
					ThisPin->AddActiveSubtitle(Subtitle, Duration, StartOffset, Timing);
				}
			}
		);
	}
}

void USubtitlesSubsystem::AddActiveSubtitle(const FSubtitleAssetData& Subtitle, float Duration, const float StartOffset, const ESubtitleTiming Timing)
{
	// If the subtitle is already active then update its duration (by removing it then and then re-adding it)
	const FActiveSubtitle* FoundActiveSubtitle = ActiveSubtitles.FindByPredicate(
		[&Subtitle](const FActiveSubtitle& ActiveSubtitle) { return ActiveSubtitle.Subtitle == Subtitle; });
	if (FoundActiveSubtitle != nullptr)
	{
		RemoveActiveSubtitle(Subtitle);
	}

	FActiveSubtitle NewActiveSubtitle{ Subtitle };

	// Subtitles with delayed offset need a timer to await their entry in the queue.
	if (StartOffset > 0.f)
	{
		// The timer handle will be reused for the duration when it enters the active subtitle queue.
		// For now the timer tracks how long until it enters that queue.
		FTimerDelegate Delegate;
		Delegate.BindUFunction(this, FName(TEXT("MakeDelayedSubtitleActive")), NewActiveSubtitle.Subtitle, Timing);
		GetWorldRef().GetTimerManager().SetTimer(NewActiveSubtitle.DurationTimerHandle, MoveTemp(Delegate), StartOffset, /*bLoop=*/false);

		DelayedSubtitles.Add(MoveTemp(NewActiveSubtitle));
	}
	else
	{
		if (Timing == ESubtitleTiming::InternallyTimed)
		{
			// Without the delayed offset, instantly enter the queue as usual.
			// The timer here tracks how long until the subtitle will expire and leave the active subtitle queue.
			FTimerDelegate Delegate;
			Delegate.BindUFunction(this, FName(TEXT("RemoveActiveSubtitle")), NewActiveSubtitle.Subtitle);
			Duration = FMath::Max(Duration, SubtitleMinDuration);
			GetWorldRef().GetTimerManager().SetTimer(NewActiveSubtitle.DurationTimerHandle, MoveTemp(Delegate), Duration, /*bLoop=*/false);
		}
		
		AddAndDisplaySubtitle(NewActiveSubtitle);
	}
}

void USubtitlesSubsystem::MakeDelayedSubtitleActive(const FSubtitleAssetData& Subtitle, const ESubtitleTiming Timing)
{
	FActiveSubtitle* DelayedSubtitle = Algo::FindByPredicate(DelayedSubtitles, [&Subtitle](const FActiveSubtitle& DelayedSubtitle) {return DelayedSubtitle.Subtitle == Subtitle; });

	if (DelayedSubtitle != nullptr)
	{
		const FSubtitleAssetData& SubtitleAsset = DelayedSubtitle->Subtitle;
		const float Duration = FMath::Max(SubtitleAsset.Duration, SubtitleMinDuration);

		if (Timing == ESubtitleTiming::InternallyTimed)
		{
			// Reuse the Timer Handle for duration, now that it's no longer needed for the delay.
			FTimerDelegate Delegate;
			Delegate.BindUFunction(this, FName(TEXT("RemoveActiveSubtitle")), DelayedSubtitle->Subtitle);
			GetWorldRef().GetTimerManager().SetTimer(DelayedSubtitle->DurationTimerHandle, MoveTemp(Delegate), Duration, /*bLoop=*/false);
		}

		// Insert the new subtitle to the actual queue and ensure it remains sorted by priority.
		AddAndDisplaySubtitle(*DelayedSubtitle);

		// Remove from the list of Delayed Subtitles
		const int32 FirstRemovedIndex = Algo::RemoveIf(DelayedSubtitles, [&Subtitle](const FActiveSubtitle& DelayedSubtitle) {return DelayedSubtitle.Subtitle == Subtitle; });
		DelayedSubtitles.SetNum(FirstRemovedIndex);
	}
}

void USubtitlesSubsystem::AddAndDisplaySubtitle(FActiveSubtitle& NewActiveSubtitle)
{
	const FSubtitleAssetData& NewSubtitle = NewActiveSubtitle.Subtitle;

	bool ShouldAddSubtitle = true;
	// Warn if the subtitle won't play due to being lower-priority than the currently-playing subtitle in that category.
	for (const FActiveSubtitle& PreQueuedSubtitle : ActiveSubtitles)
	{
		const FSubtitleAssetData& PreQueuedSubtitleData = PreQueuedSubtitle.Subtitle;
		if (PreQueuedSubtitleData.SubtitleType == NewSubtitle.SubtitleType && (PreQueuedSubtitleData.Priority >= NewSubtitle.Priority))
		{
			UE_LOG(LogSubtitlesAndClosedCaptions, Display, TEXT("Subtitle %s won't display: its priority is lower than or equal to the currently-playing subtitle in that category."), *NewSubtitle.Text.ToString());
			ShouldAddSubtitle = false;
		}
	}

	if (ShouldAddSubtitle)
	{
		ActiveSubtitles.Add(MoveTemp(NewActiveSubtitle));
		ActiveSubtitles.StableSort([](const FActiveSubtitle& Lhs, const FActiveSubtitle& Rhs) { return Lhs.Subtitle.Priority > Rhs.Subtitle.Priority; });

		UpdateWidgetData();
	}
}

bool USubtitlesSubsystem::IsSubtitleActive(const FSubtitleAssetData& Data) const
{
	if (!ensureMsgf(IsInGameThread(), TEXT("IsSubtitleActive must currently be run on the GameThread - ActiveSubtitles vector is not locked")))
	{
		return false;
	}

	const FActiveSubtitle* FoundActiveSubtitle = ActiveSubtitles.FindByPredicate(
		[Data](const FActiveSubtitle& ActiveSubtitle) { return ActiveSubtitle.Subtitle == Data; });

	return FoundActiveSubtitle != nullptr;
}

void USubtitlesSubsystem::StopSubtitle(const FSubtitleAssetData& Data)
{
	RemoveActiveSubtitle(Data);
}

void USubtitlesSubsystem::StopAllSubtitles()
{
	// Clean up queued subtitles.
	FTimerManager& TimerManager = GetWorldRef().GetTimerManager();
	for (FActiveSubtitle& ActiveSubtitle : ActiveSubtitles)
	{
		TimerManager.ClearTimer(ActiveSubtitle.DurationTimerHandle);
	}
	ActiveSubtitles.Empty();

	// Also remove delayed-start subtitles not yet in the queue.
	for (FActiveSubtitle& DelayedSubtitle : DelayedSubtitles)
	{
		TimerManager.ClearTimer(DelayedSubtitle.DurationTimerHandle);
	}
	DelayedSubtitles.Empty();

	// Clear the widget's display
	if (IsValid(SubtitleWidget))
	{
		SubtitleWidget->StopDisplayingSubtitle(ESubtitleType::AudioDescription);
		SubtitleWidget->StopDisplayingSubtitle(ESubtitleType::ClosedCaption);
		SubtitleWidget->StopDisplayingSubtitle(ESubtitleType::Subtitle);
	}
	else 
	{
		UE_LOG(LogSubtitlesAndClosedCaptions, Warning, TEXT("Can't remove subtitles because there isn't a valid UMG widget."));
	}
}

void USubtitlesSubsystem::ReplaceWidget(const TSubclassOf<USubtitleWidget>& NewWidgetAsset)
{
	if (!ActiveSubtitles.IsEmpty() || !DelayedSubtitles.IsEmpty())
	{
		UE_LOG(LogSubtitlesAndClosedCaptions, Error, TEXT("Can't replace the subtitle widget: there are still subtitles queued or displaying. Use StopAllSubtitles() first."));
		return;
	}

	if (!TryCreateUMGWidgetFromAsset(NewWidgetAsset))
	{
		UE_LOG(LogSubtitlesAndClosedCaptions, Error, TEXT("Can't replace the subtitle widget; was a valid asset provided?"));
	}
}

void USubtitlesSubsystem::RemoveActiveSubtitle(const FSubtitleAssetData& Subtitle)
{
	bool bSuccessfullyRemoved = false;
	int32 FirstRemovedIndex = Algo::StableRemoveIf(ActiveSubtitles, [&Subtitle](const FActiveSubtitle& ActiveSubtitle) {return ActiveSubtitle.Subtitle == Subtitle; });

	FTimerManager& TimerManager = GetWorldRef().GetTimerManager();
	for (int32 Index = FirstRemovedIndex; Index < ActiveSubtitles.Num(); ++Index)
	{
		TimerManager.ClearTimer(ActiveSubtitles[Index].DurationTimerHandle);
		bSuccessfullyRemoved = true;
	}

	ActiveSubtitles.SetNum(FirstRemovedIndex);

	// Stop Displaying the removed subtitle and display a newly-most-relevant one if applicable.
	if (bSuccessfullyRemoved && IsValid(SubtitleWidget))
	{
		SubtitleWidget->StopDisplayingSubtitle(Subtitle.SubtitleType);

		if (!ActiveSubtitles.IsEmpty())
		{
			const FSubtitleAssetData& HighestPrioritySubtitle = ActiveSubtitles[0].Subtitle;
			SubtitleWidget->StartDisplayingSubtitle(HighestPrioritySubtitle);
		}
	}
	else if(!IsValid(SubtitleWidget))
	{
		UE_LOG(LogSubtitlesAndClosedCaptions, Warning, TEXT("Can't remove subtitles because there isn't a valid UMG widget."));
	}

	// Also remove delayed-start subtitles using this asset.
	FirstRemovedIndex = Algo::RemoveIf(DelayedSubtitles, [&Subtitle](const FActiveSubtitle& DelayedSubtitle) {return DelayedSubtitle.Subtitle == Subtitle; });

	for (int32 Index = FirstRemovedIndex; Index < DelayedSubtitles.Num(); ++Index)
	{
		TimerManager.ClearTimer(DelayedSubtitles[Index].DurationTimerHandle);
	}
	DelayedSubtitles.SetNum(FirstRemovedIndex);
}

bool USubtitlesSubsystem::TryCreateUMGWidgetFromAsset(const TSubclassOf<USubtitleWidget>& WidgetToUse)
{
	if (IsValid(WidgetToUse))
	{
		SubtitleWidget = CreateWidget<USubtitleWidget>(GetWorld(), WidgetToUse);
		bInitializedWidget = false;	// The new widget will need to be added to the viewport.
	}

	return IsValid(SubtitleWidget);
}

bool USubtitlesSubsystem::TryCreateUMGWidget()
{
	// Set up the UMG widget
	const USubtitlesSettings* Settings = GetDefault<USubtitlesSettings>();
	check(Settings != nullptr);
	const TSubclassOf<USubtitleWidget>& WidgetToUse = Settings->GetWidget();
	
	if (!TryCreateUMGWidgetFromAsset(WidgetToUse))
	{
		// Fallback to default widget (not set by user):
		const TSubclassOf<USubtitleWidget>& WidgetToUseDefault = Settings->GetWidgetDefault();
		if (IsValid(WidgetToUseDefault))
		{
			SubtitleWidget = CreateWidget<USubtitleWidget>(GetWorld(), WidgetToUseDefault);
		}
		else
		{
			UE_LOG(LogSubtitlesAndClosedCaptions, Error, TEXT("The default Subtitle Widget asset isn't valid. Subtitles won't be displayed."));
		}
	}

	bInitializedWidget = false;
	return IsValid(SubtitleWidget);
}

void USubtitlesSubsystem::UpdateWidgetData()
{
	// Update the widget. If it's not valid (eg, destroyed on non-seamless travel), try re-creating it first.
	if (IsValid(SubtitleWidget) || (bInitializedWidget && TryCreateUMGWidget() && IsValid(SubtitleWidget)))
	{
		if (!bInitializedWidget)
		{
			SubtitleWidget->AddToViewport();
			SubtitleWidget->SetVisibility(ESlateVisibility::HitTestInvisible);
			bInitializedWidget = true;
		}

		const FSubtitleAssetData& NewHighestPrioritySubtitle = ActiveSubtitles[0].Subtitle;
		SubtitleWidget->StartDisplayingSubtitle(NewHighestPrioritySubtitle);
	}
	else
	{
		UE_LOG(LogSubtitlesAndClosedCaptions, Warning, TEXT("Can't display subtitles because there isn't a valid UMG widget to display it to (check Project Settings)."));
	}
}


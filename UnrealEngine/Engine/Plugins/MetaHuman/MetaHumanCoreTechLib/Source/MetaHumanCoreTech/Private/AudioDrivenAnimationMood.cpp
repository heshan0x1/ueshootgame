// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_EDITOR

#include "AudioDrivenAnimationMood.h"

#include "SEnumGroupsWidget.h"



void SAudioDrivenAnimationMood::Construct(const FArguments& InArgs, bool bInOffline, TSharedRef<IPropertyHandle> InMoodPropertyHandle)
{
	using MoodGroup = SEnumGroupsWidget<EAudioDrivenAnimationMood>::EnumGroup;

	TArray<MoodGroup> EnumGroups;

	if (bInOffline)
	{
		MoodGroup AutoDetect{ FText::FromString("Auto Detect"), {
			EAudioDrivenAnimationMood::AutoDetect
		} };

		EnumGroups.Add(AutoDetect);
	}
	MoodGroup Neutral{ FText::FromString("Neutral"), {
		EAudioDrivenAnimationMood::Neutral
	} };
	MoodGroup Happiness{ FText::FromString("Happy"), {
		EAudioDrivenAnimationMood::Happiness,
		EAudioDrivenAnimationMood::Confidence,
		EAudioDrivenAnimationMood::Excitement,
		EAudioDrivenAnimationMood::Playfulness
	} };
	MoodGroup Sadness{ FText::FromString("Sad"), {
		EAudioDrivenAnimationMood::Sadness,
		EAudioDrivenAnimationMood::Boredom
	} };
	MoodGroup Disgust{ FText::FromString("Disgust"), {
		EAudioDrivenAnimationMood::Disgust
	} };
	MoodGroup Anger{ FText::FromString("Anger"), {
		EAudioDrivenAnimationMood::Anger}
	};
	MoodGroup Surprise{ FText::FromString("Surprise"), {
		EAudioDrivenAnimationMood::Surprise
	} };
	MoodGroup Fear{ FText::FromString("Fear"), {
		EAudioDrivenAnimationMood::Fear,
		EAudioDrivenAnimationMood::Confusion
	} };

	EnumGroups.Append({ Neutral, Happiness, Sadness, Fear, Disgust, Anger, Surprise });

	EAudioDrivenAnimationMood InitialMood = bInOffline ? EAudioDrivenAnimationMood::AutoDetect : EAudioDrivenAnimationMood::Neutral;
	uint8 MoodPropertyValue;
	if (const FPropertyAccess::Result Result = InMoodPropertyHandle->GetValue(MoodPropertyValue); Result == FPropertyAccess::Success)
	{
		InitialMood = static_cast<EAudioDrivenAnimationMood>(MoodPropertyValue);
	}

	ChildSlot
	[
		SNew(SEnumGroupsWidget<EAudioDrivenAnimationMood>)
		.InitiallySelectedItem(InitialMood)
		.EnumGroups(EnumGroups)
		.OnSelectionChanged_Lambda([InMoodPropertyHandle](EAudioDrivenAnimationMood SelectedMood)
		{
			InMoodPropertyHandle->SetValue(static_cast<uint8>(SelectedMood));
		})
	];
}

#endif

// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanAudioBaseLiveLinkSubjectSettings.h"

#include "MetaHumanAudioBaseLiveLinkSubject.h"



#if WITH_EDITOR
void UMetaHumanAudioBaseLiveLinkSubjectSettings::PostEditChangeProperty(struct FPropertyChangedEvent& InPropertyChangedEvent)
{
	Super::PostEditChangeProperty(InPropertyChangedEvent);

	if (FProperty* Property = InPropertyChangedEvent.Property)
	{
		const FName PropertyName = *Property->GetName();

		const bool bMoodChanged = PropertyName == GET_MEMBER_NAME_CHECKED(ThisClass, Mood);
		const bool bMoodIntensityChanged = PropertyName == GET_MEMBER_NAME_CHECKED(ThisClass, MoodIntensity);
		const bool bLookaheadChanged = PropertyName == GET_MEMBER_NAME_CHECKED(ThisClass, Lookahead);

		FMetaHumanAudioBaseLiveLinkSubject* AudioSubject = (FMetaHumanAudioBaseLiveLinkSubject*) Subject;

		if (bMoodChanged || bMoodIntensityChanged)
		{
			AudioSubject->SetMood(Mood, MoodIntensity);
		}
		else if (bLookaheadChanged)
		{
			AudioSubject->SetLookahead(Lookahead);
		}
	}
}
#endif

void UMetaHumanAudioBaseLiveLinkSubjectSettings::Setup()
{
	Super::Setup();

	// No calibration, smoothing or head translation required.
	// Calibration and head translations are no-ops unless configured,
	// but we do need to explicitly set smoothing parameters to null.
	Parameters = nullptr;
}

void UMetaHumanAudioBaseLiveLinkSubjectSettings::SetMoodIntensity(float InMoodIntensity)
{
	MoodIntensity = FMath::Clamp(InMoodIntensity, 0.0, 1.0);

	FMetaHumanAudioBaseLiveLinkSubject* AudioSubject = (FMetaHumanAudioBaseLiveLinkSubject*) Subject;
	AudioSubject->SetMood(Mood, MoodIntensity);
}

void UMetaHumanAudioBaseLiveLinkSubjectSettings::GetMoodIntensity(float& OutMoodIntensity) const
{
	OutMoodIntensity = MoodIntensity;
}

void UMetaHumanAudioBaseLiveLinkSubjectSettings::SetMood(EAudioDrivenAnimationMood InMood)
{
	Mood = InMood;

	FMetaHumanAudioBaseLiveLinkSubject* AudioSubject = (FMetaHumanAudioBaseLiveLinkSubject*) Subject;
	AudioSubject->SetMood(Mood, MoodIntensity);
}

void UMetaHumanAudioBaseLiveLinkSubjectSettings::GetMood(EAudioDrivenAnimationMood& OutMood) const
{
	OutMood = Mood;
}

void UMetaHumanAudioBaseLiveLinkSubjectSettings::SetLookahead(int32 InLookahead)
{
	Lookahead = FMath::Clamp((InLookahead/20) * 20, 80, 240);

	FMetaHumanAudioBaseLiveLinkSubject* AudioSubject = (FMetaHumanAudioBaseLiveLinkSubject*) Subject;
	AudioSubject->SetLookahead(Lookahead);
}

void UMetaHumanAudioBaseLiveLinkSubjectSettings::GetLookahead(int32& OutLookahead) const
{
	OutLookahead = Lookahead;
}

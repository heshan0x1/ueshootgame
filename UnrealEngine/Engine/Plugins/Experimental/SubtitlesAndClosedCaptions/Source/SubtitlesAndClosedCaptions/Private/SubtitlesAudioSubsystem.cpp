// Copyright Epic Games, Inc. All Rights Reserved.

#include "SubtitlesAudioSubsystem.h"

#include "ActiveSound.h"
#include "Sound/DialogueSoundWaveProxy.h"
#include "Sound/SoundCue.h"
#include "Sound/SoundNodeDialoguePlayer.h"
#include "Subtitles/SubtitlesAndClosedCaptionsDelegates.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(SubtitlesAudioSubsystem)

void USubtitlesAudioSubsystem::NotifyActiveSoundCreated(FActiveSound& ActiveSound)
{
	USoundBase* Sound = ActiveSound.GetSound();

	if (IsValid(Sound))
	{
		if (Sound->HasAssetUserDataOfClass(USubtitleAssetUserData::StaticClass()))
		{
			const USubtitleAssetUserData* Subtitles = CastChecked<USubtitleAssetUserData>(Sound->GetAssetUserDataOfClass(USubtitleAssetUserData::StaticClass()));
			for (const FSubtitleAssetData& Subtitle : Subtitles->Subtitles)
			{
				const bool bUseDurationProperty = (Subtitle.SubtitleDurationType == ESubtitleDurationType::UseDurationProperty);
				FSubtitlesAndClosedCaptionsDelegates::QueueSubtitle.ExecuteIfBound(FQueueSubtitleParameters{ Subtitle }, bUseDurationProperty ? ESubtitleTiming::InternallyTimed : ESubtitleTiming::ExternallyTimed);
			}
		}

		// Also check for DialogueWaves with localized text
		// DialogueWaves, even inside SoundCues, should only play subtitles this way if the migration cvar was set.
		// Since this callback is run when the sound is created in the sound engine, changing the cvar doesn't effect currently-playing sounds.
		static const IConsoleVariable* CVarUseNewSubtitles = IConsoleManager::Get().FindConsoleVariable(TEXT("au.UseNewSubtitles"));

		if (CVarUseNewSubtitles && CVarUseNewSubtitles->GetBool() && ActiveSound.bHandleSubtitles)
		{
			FSubtitleAssetData SubtitleCue;

			const UDialogueSoundWaveProxy* DialogueWave = Cast<UDialogueSoundWaveProxy>(Sound);
			if (IsValid(DialogueWave))
			{
				QueueSubtitleFromDialogueWaveProxy(DialogueWave, ActiveSound);
			}
			else
			{
				// Check inside Sound Cues for DialogueWaves as well.
				const USoundCue* SoundCue = Cast<USoundCue>(Sound);
				if (IsValid(SoundCue))
				{
					const bool bQueuedFromTopLevelNode = CheckSoundCueNodeForDialogueWaves(SoundCue->FirstNode, ActiveSound);

					// If the parent node is not a DialogueWavePlayer, check for nested nodes that may also have DialogueWaves
					if(!bQueuedFromTopLevelNode)
					{
						TArray<USoundNode*> ChildNodes;
						SoundCue->FirstNode->GetAllNodes(ChildNodes);
						for (const USoundNode* Node : ChildNodes)
						{
							CheckSoundCueNodeForDialogueWaves(Node, ActiveSound);
						}
					}
				}
			}
		}
	}
}

void USubtitlesAudioSubsystem::NotifyActiveSoundDeleting(const FActiveSound& ActiveSound)
{
	USoundBase* Sound = ActiveSound.GetSound();

	if (IsValid(Sound))
	{
		if (Sound->HasAssetUserDataOfClass(USubtitleAssetUserData::StaticClass()))
		{
			if (Sound->HasAssetUserDataOfClass(USubtitleAssetUserData::StaticClass()))
			{
				const USubtitleAssetUserData* Subtitles = CastChecked<USubtitleAssetUserData>(Sound->GetAssetUserDataOfClass(USubtitleAssetUserData::StaticClass()));
				for (const FSubtitleAssetData& Subtitle : Subtitles->Subtitles)
				{
					const bool bUseDurationProperty = (Subtitle.SubtitleDurationType == ESubtitleDurationType::UseDurationProperty);
					if (!bUseDurationProperty)
					{
						FSubtitlesAndClosedCaptionsDelegates::StopSubtitle.ExecuteIfBound(Subtitle);
					}
				}
			}
		}
	}
}

bool USubtitlesAudioSubsystem::CheckSoundCueNodeForDialogueWaves(const USoundNode* Node, const FActiveSound& ActiveSound)
{
	const USoundNodeDialoguePlayer* DialoguePlayerNode = Cast<USoundNodeDialoguePlayer>(Node);

	// These pointers might be null or invalid:
	// A SoundCue might have a Dialogue node that doesn't actually reference any assets, or the asset itself might lack any actual soundwaves to play.
	if (IsValid(DialoguePlayerNode))
	{
		const UDialogueSoundWaveProxy* Proxy = DialoguePlayerNode->GetContextualDialogueSoundWaveProxy();
		if (IsValid(Proxy))
		{
			QueueSubtitleFromDialogueWaveProxy(Proxy, ActiveSound);
			return true;
		}
	}

	return false;
}

void USubtitlesAudioSubsystem::QueueSubtitleFromDialogueWaveProxy(const UDialogueSoundWaveProxy* DialogueWaveProxy, const FActiveSound& ActiveSound)
{
	check(ActiveSound.bHandleSubtitles);	// Function should only be called if the Active Sound actually has subtitles it needs queued.
	FSubtitleAssetData SubtitleCue;
	SubtitleCue.Text = DialogueWaveProxy->GetSubtitleText();
	SubtitleCue.Priority = ActiveSound.SubtitlePriority;
	SubtitleCue.Duration = DialogueWaveProxy->GetDuration();
	SubtitleCue.StartOffset = ActiveSound.RequestedStartTime;

	FSubtitlesAndClosedCaptionsDelegates::QueueSubtitle.ExecuteIfBound(FQueueSubtitleParameters{ SubtitleCue }, ESubtitleTiming::InternallyTimed);
}

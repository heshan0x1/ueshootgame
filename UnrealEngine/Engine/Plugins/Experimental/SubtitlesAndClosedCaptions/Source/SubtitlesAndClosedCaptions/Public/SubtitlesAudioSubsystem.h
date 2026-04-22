// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once 
#include "Subsystems/AudioEngineSubsystem.h"
#include "ActiveSoundUpdateInterface.h"
#include "SubtitlesAudioSubsystem.generated.h"

#define UE_API SUBTITLESANDCLOSEDCAPTIONS_API

class UDialogueSoundWaveProxy;
class USoundNode;

/*
*	SubtitlesAudioSubsystem - AudioEngineSubsystem for automatically queueing subtitles on audio classes.
*
*	Separated from SubtitlesSubsystem, which is a UWorldSubsystem, to avoid issues with lifecycle management
*	as some parts need access to the world and others to the audio device.
*/
UCLASS(MinimalAPI, config = Game, defaultConfig)
class USubtitlesAudioSubsystem : public UAudioEngineSubsystem
	, public IActiveSoundUpdateInterface
{
	GENERATED_BODY()
public:
	USubtitlesAudioSubsystem() = default;

	//~ Begin IActiveSoundUpdateInterface
	virtual void NotifyActiveSoundCreated(FActiveSound& ActiveSound) override;
	virtual void NotifyActiveSoundDeleting(const FActiveSound& ActiveSound) override;
	//~ End IActiveSoundUpdateInterface

private:
	// Helper functions to cut down on duplicated code
	static bool CheckSoundCueNodeForDialogueWaves(const USoundNode* Node, const FActiveSound& ActiveSound);
	static void QueueSubtitleFromDialogueWaveProxy(const UDialogueSoundWaveProxy* DialogueWaveProxy, const FActiveSound& ActiveSound);
};

#undef UE_API

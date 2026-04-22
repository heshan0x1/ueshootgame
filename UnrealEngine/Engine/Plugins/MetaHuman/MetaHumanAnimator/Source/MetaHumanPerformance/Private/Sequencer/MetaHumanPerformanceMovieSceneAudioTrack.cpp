// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanPerformanceMovieSceneAudioTrack.h"
#include "MetaHumanPerformanceMovieSceneAudioSection.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MetaHumanPerformanceMovieSceneAudioTrack)


UMetaHumanPerformanceMovieSceneAudioTrack::UMetaHumanPerformanceMovieSceneAudioTrack(const FObjectInitializer& InObjectInitializer)
	: Super{ InObjectInitializer }
{
	// This disables the "Add Section" entry in the track's context menu
	SupportedBlendTypes = FMovieSceneBlendTypeField::None();
}

UMovieSceneSection* UMetaHumanPerformanceMovieSceneAudioTrack::CreateNewSection()
{
	return NewObject<UMetaHumanPerformanceMovieSceneAudioSection>(this, NAME_None, RF_Transactional);
}

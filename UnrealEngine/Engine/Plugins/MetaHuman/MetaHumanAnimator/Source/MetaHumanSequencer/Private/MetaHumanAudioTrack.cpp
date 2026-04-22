// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanAudioTrack.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MetaHumanAudioTrack)


UMetaHumanAudioTrack::UMetaHumanAudioTrack(const FObjectInitializer& InObjectInitializer)
	: Super{ InObjectInitializer }
{
	// This disables the "Add Section" entry in the track's context menu
	SupportedBlendTypes = FMovieSceneBlendTypeField::None();
}

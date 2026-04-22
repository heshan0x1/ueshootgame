// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MovieSceneMediaSection.h"
#include "MetaHumanMovieSceneChannel.h"

#include "MetaHumanMovieSceneMediaSection.generated.h"

#define UE_API METAHUMANSEQUENCER_API

/**
 * Implements a MovieSceneMediaSection
 */

UCLASS(MinimalAPI)
class UMetaHumanMovieSceneMediaSection
	: public UMovieSceneMediaSection
{
	GENERATED_BODY()

public:
	UE_API UMetaHumanMovieSceneMediaSection(const FObjectInitializer& ObjectInitializer);

	UE_API FMovieSceneChannelDataKeyAddedEvent& OnKeyAddedEventDelegate();
	UE_API FMovieSceneChannelDataKeyDeletedEvent& OnKeyDeletedEventDelegate();
	
	UE_API FMetaHumanMovieSceneChannel& GetMetaHumanChannelRef();
	
	UE_API void AddChannelToMovieSceneSection();

private:

	UPROPERTY()
	FMetaHumanMovieSceneChannel MetaHumanChannel;
};

#undef UE_API

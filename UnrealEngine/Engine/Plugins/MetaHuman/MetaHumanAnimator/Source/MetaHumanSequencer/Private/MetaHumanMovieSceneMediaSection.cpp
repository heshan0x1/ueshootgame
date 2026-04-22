// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanMovieSceneMediaSection.h"
#include "Channels/MovieSceneChannelProxy.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MetaHumanMovieSceneMediaSection)

struct FMetaHumanMediaSectionEditorData
{
	FMetaHumanMediaSectionEditorData()
	{
		Data.SetIdentifiers("KeyFrame", NSLOCTEXT("MetaHumanMovieSceneMediaSection", "KeyFrameText", "KeyFrame"));
	}

	FMovieSceneChannelMetaData Data;
};

UMetaHumanMovieSceneMediaSection::UMetaHumanMovieSceneMediaSection(const FObjectInitializer& ObjectInitializer)
	: Super{ ObjectInitializer }
{
}

FMovieSceneChannelDataKeyAddedEvent& UMetaHumanMovieSceneMediaSection::OnKeyAddedEventDelegate()
{
	return MetaHumanChannel.OnKeyAddedEvent();
}

FMovieSceneChannelDataKeyDeletedEvent& UMetaHumanMovieSceneMediaSection::OnKeyDeletedEventDelegate()
{
	return MetaHumanChannel.OnKeyDeletedEvent();
}

FMetaHumanMovieSceneChannel& UMetaHumanMovieSceneMediaSection::GetMetaHumanChannelRef()
{
	return MetaHumanChannel;
}

void UMetaHumanMovieSceneMediaSection::AddChannelToMovieSceneSection()
{
	FMovieSceneChannelProxyData Channels;
	FMetaHumanMediaSectionEditorData EditorData;

	Channels.Add(MetaHumanChannel, EditorData.Data, TMovieSceneExternalValue<bool>());
	ChannelProxy = MakeShared<FMovieSceneChannelProxy>(MoveTemp(Channels));
}

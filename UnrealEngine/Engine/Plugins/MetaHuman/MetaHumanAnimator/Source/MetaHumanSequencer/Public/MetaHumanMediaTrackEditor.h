// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Sequencer/MediaTrackEditor.h"

#define UE_API METAHUMANSEQUENCER_API

/**
 * MediaTrackEditor that can be added to MetaHumanSequences
 * This can be used to customize the behavior of the sequencer track editor
 * Right now this relies on the functionality available in FMediaTrackEditor
 */
class FMetaHumanMediaTrackEditor
	: public FMediaTrackEditor
{
public:

	/**
	 * Create a new track editor instance. This is called by ISequencerModule::RegisterPropertyTrackEditor when
	 * registering this editor
	 */
	static TSharedRef<ISequencerTrackEditor> CreateTrackEditor(TSharedRef<ISequencer> InOwningSequencer)
	{
		return MakeShared<FMetaHumanMediaTrackEditor>(InOwningSequencer);
	}

	UE_API FMetaHumanMediaTrackEditor(TSharedRef<ISequencer> InSequencer);

	//~ FMovieSceneTrackEditor interface
	UE_API virtual bool SupportsSequence(class UMovieSceneSequence* InSequence) const override;
	UE_API virtual bool SupportsType(TSubclassOf<UMovieSceneTrack> TrackClass) const override;
	UE_API virtual void BuildAddTrackMenu(FMenuBuilder& MenuBuilder) override;
	UE_API virtual TSharedPtr<SWidget> BuildOutlinerEditWidget(const FGuid& ObjectBinding, UMovieSceneTrack* Track, const FBuildEditWidgetParams& Params) override;
	UE_API virtual TSharedRef<ISequencerSection> MakeSectionInterface(UMovieSceneSection& SectionObject, UMovieSceneTrack& Track, FGuid ObjectBinding) override;
	UE_API virtual bool IsResizable(UMovieSceneTrack* InTrack) const override;
	UE_API virtual void Resize(float NewSize, UMovieSceneTrack* InTrack) override;
};

#undef UE_API

// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MetaHumanMovieSceneChannel.h"
#include "Sequencer/MediaThumbnailSection.h"
#include "SequencerSectionPainter.h"

#define UE_API METAHUMANSEQUENCER_API

class ISequencer;
class UMovieSceneSection;


/**
 * Extends FMediaThumbnailSection to allow painting on top of the sequencer section
 */
class FMetaHumanMediaSection
	: public FMediaThumbnailSection
{
public:
	UE_API FMetaHumanMediaSection(UMovieSceneMediaSection& InSection, TSharedPtr<class FTrackEditorThumbnailPool> InThumbnailPool, TSharedPtr<class ISequencer> InSequencer);

	//~ ISequencerSection interface
	UE_API virtual bool IsReadOnly() const override;
	UE_API virtual bool SectionIsResizable() const override;
	UE_API virtual int32 OnPaintSection(FSequencerSectionPainter& InPainter) const override;
	UE_API virtual float GetSectionHeight(const UE::Sequencer::FViewDensityInfo& InViewDensity) const override;
	UE_API virtual FText GetSectionTitle() const override;

private:

	FMetaHumanMovieSceneChannel* KeyContainer = nullptr;
};

/**
 * Helper to paint excluded frames using FSequencerSectionPainter
 */
namespace MetaHumanSectionPainterHelper
{
	METAHUMANSEQUENCER_API int32 PaintExcludedFrames(FSequencerSectionPainter& InPainter, int32 InLayerId, ISequencer* InSequencer, UMovieSceneSection* InSection);
};

#undef UE_API

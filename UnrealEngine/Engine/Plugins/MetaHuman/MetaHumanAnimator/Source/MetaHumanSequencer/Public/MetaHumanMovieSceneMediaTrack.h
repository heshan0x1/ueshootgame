// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MovieSceneMediaTrack.h"

#include "MetaHumanMovieSceneMediaTrack.generated.h"

#define UE_API METAHUMANSEQUENCER_API

/**
 * Implements a MovieSceneMediaTrack customized for the MetaHumanPerformance plugin
 */
UCLASS(MinimalAPI)
class UMetaHumanMovieSceneMediaTrack
	: public UMovieSceneMediaTrack
{
	GENERATED_BODY()

public:
	UE_API UMetaHumanMovieSceneMediaTrack(const FObjectInitializer& InObjectInitializer);

	//~ UMovieSceneMediaTrack interface
	UE_API virtual UMovieSceneSection* AddNewMediaSourceOnRow(UMediaSource& MediaSource, FFrameNumber Time, int32 RowIndex) override;
	UE_API virtual bool SupportsType(TSubclassOf<UMovieSceneSection> SectionClass) const override;
	UE_API virtual void RemoveAllAnimationData() override;

#if WITH_EDITORONLY_DATA

public:

	/**
	 * Get the height of this track's rows
	 */
	UE_API float GetRowHeight() const;

	/**
	 * Set the height of this track's rows
	 */
	UE_API void SetRowHeight(int32 NewRowHeight);

private:
	/** The minimum height for resizable media tracks */
	static constexpr float MinRowHeight = 37.0f;

	/** The height for each row of this track */
	UPROPERTY()
	float RowHeight;

#endif
};

#undef UE_API

// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Array.h"
#include "Containers/ArrayView.h"

#include "Math/Range.h"
#include "Misc/FrameNumber.h"
#include "Misc/FrameTime.h"
#include "Misc/Optional.h"
#include "Channels/MovieSceneChannel.h"
#include "Channels/MovieSceneChannelData.h"
#include "Channels/MovieSceneChannelTraits.h"
#include "Channels/MovieSceneChannelEditorData.h"

#include "MetaHumanMovieSceneChannel.generated.h"

#define UE_API METAHUMANSEQUENCER_API


USTRUCT()
struct FMetaHumanMovieSceneChannel : public FMovieSceneChannel
{
	GENERATED_BODY()

	FMetaHumanMovieSceneChannel()
		: DefaultValue(), bHasDefaultValue(false)
	{}

	/**
	 * Access a mutable interface for this channel's data
	 *
	 * @return An object that is able to manipulate this channel's data
	 */
	virtual inline TMovieSceneChannelData<bool> GetData()
	{
		return TMovieSceneChannelData<bool>(&Times, &Values, this, &KeyHandles);
	}

	/**
	 * Access a constant interface for this channel's data
	 *
	 * @return An object that is able to interrogate this channel's data
	 */
	virtual inline TMovieSceneChannelData<const bool> GetData() const
	{
		return TMovieSceneChannelData<const bool>(&Times, &Values);
	}

	/**
	 * Const access to this channel's times
	 */
	inline TArrayView<const FFrameNumber> GetTimes() const
	{
		return Times;
	}

	/**
	 * Const access to this channel's values
	 */
	inline TArrayView<const bool> GetValues() const
	{
		return Values;
	}

	/**
	 * Check whether this channel has any data
	 */
	inline bool HasAnyData() const
	{
		return Times.Num() != 0 || bHasDefaultValue == true;
	}

	/**
	 * Evaluate this channel
	 *
	 * @param InTime     The time to evaluate at
	 * @param OutValue   A value to receive the result
	 * @return true if the channel was evaluated successfully, false otherwise
	 */
	UE_API virtual bool Evaluate(FFrameTime InTime, bool& OutValue) const;

public:

	// ~ FMovieSceneChannel Interface
	UE_API virtual void GetKeys(const TRange<FFrameNumber>& WithinRange, TArray<FFrameNumber>* OutKeyTimes, TArray<FKeyHandle>* OutKeyHandles) override;
	UE_API virtual void GetKeyTimes(TArrayView<const FKeyHandle> InHandles, TArrayView<FFrameNumber> OutKeyTimes) override;
	UE_API virtual void SetKeyTimes(TArrayView<const FKeyHandle> InHandles, TArrayView<const FFrameNumber> InKeyTimes) override;
	UE_API virtual void DuplicateKeys(TArrayView<const FKeyHandle> InHandles, TArrayView<FKeyHandle> OutNewHandles) override;
	UE_API virtual void DeleteKeys(TArrayView<const FKeyHandle> InHandles) override;
	UE_API virtual void DeleteKeysFrom(FFrameNumber InTime, bool bDeleteKeysBefore) override;
	UE_API virtual void RemapTimes(const UE::MovieScene::IRetimingInterface& Retimer) override;
	UE_API virtual TRange<FFrameNumber> ComputeEffectiveRange() const override;
	UE_API virtual int32 GetNumKeys() const override;
	UE_API virtual void Reset() override;
	UE_API virtual void Offset(FFrameNumber DeltaPosition) override;
	UE_API virtual void Optimize(const FKeyDataOptimizationParams& InParameters) override;
	UE_API virtual void ClearDefault() override;

public:

	/**
	 * Set this channel's default value that should be used when no keys are present
	 *
	 * @param InDefaultValue The desired default value
	 */
	inline void SetDefault(bool InDefaultValue)
	{
		bHasDefaultValue = true;
		DefaultValue = InDefaultValue;
	}

	/**
	 * Get this channel's default value that will be used when no keys are present
	 *
	 * @return (Optional) The channel's default value
	 */
	inline TOptional<bool> GetDefault() const
	{
		return bHasDefaultValue ? TOptional<bool>(DefaultValue) : TOptional<bool>();
	}

	/**
	 * Remove this channel's default value causing the channel to have no effect where no keys are present
	 */
	inline void RemoveDefault()
	{
		bHasDefaultValue = false;
	}

protected:

	UPROPERTY(meta=(KeyTimes))
	TArray<FFrameNumber> Times;

	UPROPERTY()
	bool DefaultValue;

	UPROPERTY()
	bool bHasDefaultValue;

	UPROPERTY(meta=(KeyValues))
	TArray<bool> Values;

	FMovieSceneKeyHandleMap KeyHandles;
};

template<>
struct TMovieSceneChannelTraits<FMetaHumanMovieSceneChannel> : TMovieSceneChannelTraitsBase<FMetaHumanMovieSceneChannel>
{
	enum { SupportsDefaults = false };
	
	typedef TMovieSceneExternalValue<bool> ExtendedEditorDataType;
};

#undef UE_API

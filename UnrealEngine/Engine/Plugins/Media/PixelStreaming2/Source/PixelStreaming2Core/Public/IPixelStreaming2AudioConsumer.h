// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Interface.h"

#include "IPixelStreaming2AudioConsumer.generated.h"

UINTERFACE(MinimalAPI)
class UPixelStreaming2AudioConsumer : public UInterface
{
	GENERATED_BODY()
};

/**
 * An "Audio Consumer" is an object that is responsible for outputting the audio received from a peer. For example, by
 * passing the audio into a UE submix.
 */
class IPixelStreaming2AudioConsumer
{
	GENERATED_BODY()

public:
	/**
	 * @brief Consume raw audio data.
	 * @param AudioData Pointer to the audio data.
	 * @param InSampleRate Audio sample rate in samples per second.
	 * @param NChannels Number of audio channels. For example 2 for stero audio.
	 * @param NFrames Number of Audio frames in a single channel.
	 */
	virtual void ConsumeRawPCM(const int16_t* AudioData, int InSampleRate, size_t NChannels, size_t NFrames) = 0;

	/**
	 * @brief Called when a audio consumer is added.
	 */
	virtual void OnAudioConsumerAdded() = 0;

	UE_DEPRECATED(5.7, "OnConsumerAdded has been deprecated. Please use OnAudioConsumerAdded instead.")
	virtual void OnConsumerAdded() {}

	/**
	 * @brief Called when a audio consumer is removed.
	 */
	virtual void OnAudioConsumerRemoved() = 0;
	
	UE_DEPRECATED(5.7, "OnConsumerRemoved has been deprecated. Please use OnAudioConsumerRemoved instead.")
	virtual void OnConsumerRemoved() {}
};

// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Audio.h"
#include "ISubmixBufferListener.h"
#include "IPixelStreaming2AudioProducer.h"

#define UE_API PIXELSTREAMING2_API

namespace UE::PixelStreaming2
{
	class FPatchInputProxy;

	/**
	 * An audio input capable of listening to UE submix's as well as receiving user audio via the OnPushedAudio method.
	 * Any received audio will be passed to the PatchInput's OnNewAudioFrame method
	 */
	class FAudioProducer : public ISubmixBufferListener
	{
	public:
		static UE_API TSharedPtr<FAudioProducer> Create(Audio::FDeviceId AudioDeviceId, TSharedPtr<FPatchInputProxy> PatchInput);
		static UE_API TSharedPtr<FAudioProducer> Create(TSharedPtr<IPixelStreaming2AudioProducer> AudioProducer, TSharedPtr<FPatchInputProxy> PatchInput);
		virtual ~FAudioProducer() = default;

		// ISubmixBufferListener interface
		UE_API virtual void OnNewSubmixBuffer(const USoundSubmix* OwningSubmix, float* AudioData, int32 NumSamples, int32 NumChannels, const int32 SampleRate, double AudioClock) override;

		// Listener for audio pushed from custom IPixelStreaming2AudioProducer implementations
		UE_API void OnPushedAudio(const float* AudioData, int32 NumSamples, int32 NumChannels, int32 SampleRate);

		void ToggleMuted() { bIsMuted = !bIsMuted; }

	protected:
		UE_API FAudioProducer(TSharedPtr<FPatchInputProxy> PatchInput);

	protected:
		TSharedPtr<FPatchInputProxy> PatchInput;
		bool						 bIsMuted = false;
	};
} // namespace UE::PixelStreaming2

#undef UE_API

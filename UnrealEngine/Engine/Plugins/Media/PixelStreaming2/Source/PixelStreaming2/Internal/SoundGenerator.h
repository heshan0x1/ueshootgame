// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Sound/SoundGenerator.h"

#include <atomic>

#define UE_API PIXELSTREAMING2_API

namespace UE::PixelStreaming2
{
	/**
	 * An `ISoundGenerator` implementation to pump some audio from EpicRtc into this synth component
	 */
	class FSoundGenerator : public ::ISoundGenerator
	{
	public:
		UE_API FSoundGenerator();
		virtual ~FSoundGenerator() = default;

		// Called when a new buffer is required.
		UE_API virtual int32 OnGenerateAudio(float* OutAudio, int32 NumSamples) override;

		// Returns the number of samples to render per callback
		UE_API virtual int32 GetDesiredNumSamplesToRenderPerCallback() const;

		// Optional. Called on audio generator thread right when the generator begins generating.
		virtual void OnBeginGenerate() { bGeneratingAudio = true; };

		// Optional. Called on audio generator thread right when the generator ends generating.
		virtual void OnEndGenerate() { bGeneratingAudio = false; };

		// Optional. Can be overridden to end the sound when generating is finished.
		virtual bool IsFinished() const { return false; };

		UE_API void AddAudio(const int16_t* AudioData, int InSampleRate, size_t NChannels, size_t NFrames);

		int32 GetSampleRate() { return Params.SampleRate; }
		int32 GetNumChannels() { return Params.NumChannels; }
		UE_API void  EmptyBuffers();
		UE_API void  SetParameters(const FSoundGeneratorInitParams& InitParams);

	private:
		FSoundGeneratorInitParams Params;
		TArray<int16_t>			  Buffer;
		FCriticalSection		  CriticalSection;

	public:
		std::atomic<bool> bGeneratingAudio = false;
		std::atomic<bool> bShouldGenerateAudio = false;
	};

} // namespace UE::PixelStreaming2

#undef UE_API

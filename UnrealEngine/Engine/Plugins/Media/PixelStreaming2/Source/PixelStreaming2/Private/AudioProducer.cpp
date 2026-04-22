// Copyright Epic Games, Inc. All Rights Reserved.

#include "AudioProducer.h"

#include "AudioCapturer.h"
#include "AudioDevice.h"
#include "AudioDeviceManager.h"

namespace UE::PixelStreaming2
{
	TSharedPtr<FAudioProducer> FAudioProducer::Create(Audio::FDeviceId InAudioDeviceId, TSharedPtr<FPatchInputProxy> InPatchInput)
	{
		TSharedPtr<FAudioProducer> Listener = TSharedPtr<FAudioProducer>(new FAudioProducer(InPatchInput));
		if (FAudioDeviceManager* AudioDeviceManager = FAudioDeviceManager::Get(); AudioDeviceManager != nullptr)
		{
			if (FAudioDevice* AudioDevice = AudioDeviceManager->GetAudioDeviceRaw(InAudioDeviceId); AudioDevice != nullptr)
			{
				AudioDevice->RegisterSubmixBufferListener(Listener.ToSharedRef(), AudioDevice->GetMainSubmixObject());
			}
		}

		return Listener;
	}

	TSharedPtr<FAudioProducer> FAudioProducer::Create(TSharedPtr<IPixelStreaming2AudioProducer> InAudioProducer, TSharedPtr<FPatchInputProxy> InPatchInput)
	{
		TSharedPtr<FAudioProducer> Listener = TSharedPtr<FAudioProducer>(new FAudioProducer(InPatchInput));
		InAudioProducer->OnAudioPushed.AddSP(Listener.ToSharedRef(), &FAudioProducer::OnPushedAudio);

		return Listener;
	}

	FAudioProducer::FAudioProducer(TSharedPtr<FPatchInputProxy> PatchInput)
		: PatchInput(PatchInput)
	{
	}

	void FAudioProducer::OnNewSubmixBuffer(const USoundSubmix* OwningSubmix, float* AudioData, int32 NumSamples, int32 NumChannels, const int32 SampleRate, double AudioClock)
	{
		if (bIsMuted)
		{
			return;
		}

		PatchInput->OnNewAudioFrame(AudioData, NumSamples, NumChannels, SampleRate);
	}

	void FAudioProducer::OnPushedAudio(const float* AudioData, int32 NumSamples, int32 NumChannels, int32 SampleRate)
	{
		if (bIsMuted)
		{
			return;
		}

		PatchInput->OnNewAudioFrame(AudioData, NumSamples, NumChannels, SampleRate);
	}
} // namespace UE::PixelStreaming2
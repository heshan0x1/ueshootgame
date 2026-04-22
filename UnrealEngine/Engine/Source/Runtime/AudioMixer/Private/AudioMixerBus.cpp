// Copyright Epic Games, Inc. All Rights Reserved.

#include "AudioMixerBus.h"

#include "Algo/ForEach.h"
#include "AudioMixerCVars.h"
#include "AudioMixerSourceManager.h"
#include "AudioRenderScheduler.h"
#include "DSP/AlignedBuffer.h"
#include "DSP/FloatArrayMath.h"

namespace Audio
{
	FMixerAudioBus::FMixerAudioBus(FMixerSourceManager* InSourceManager, bool bInIsAutomatic, int32 InNumChannels, const FAudioBusKey InBusKey)
		: CurrentBufferIndex(1)
		, NumChannels(InNumChannels)
		, NumFrames(InSourceManager->GetNumOutputFrames())
		, SourceManager(InSourceManager)
		, BusKey(InBusKey)
		, bIsAutomatic(bInIsAutomatic)
	{
		SetNumOutputChannels(NumChannels);

		if (AudioMixerCVars::UseRenderScheduler)
		{
			SourceManager->GetRenderScheduler().AddStep(FAudioRenderStepId::FromAudioBusKey(BusKey), this);
		}
	}

	FMixerAudioBus::~FMixerAudioBus()
	{
		if (AudioMixerCVars::UseRenderScheduler)
		{
			SourceManager->GetRenderScheduler().RemoveStep(FAudioRenderStepId::FromAudioBusKey(BusKey));
		}
	}

	void FMixerAudioBus::SetNumOutputChannels(int32 InNumOutputChannels)
	{
		NumChannels = InNumOutputChannels;
		const int32 NumSamples = NumChannels * NumFrames;
		for (int32 i = 0; i < 2; ++i)
		{
			MixedSourceData[i].Reset();
			MixedSourceData[i].AddZeroed(NumSamples);
		}
	}

	void FMixerAudioBus::Update()
	{
		// When using the scheduler we flip buffers at the beginning of MixBuffer() instead.
		check(!AudioMixerCVars::UseRenderScheduler);
		CurrentBufferIndex = 1 - CurrentBufferIndex;
	}

	void FMixerAudioBus::AddInstanceId(const int32 InSourceId, const uint64 InTransmitterID, int32 InNumOutputChannels)
	{
		InstanceIds.Add(InSourceId);
		if (AudioMixerCVars::UseRenderScheduler)
		{
			// Make sure this bus gets mixed before the source from it gets rendered
			const FAudioRenderStepId SourceStepId = FAudioRenderStepId::FromTransmitterID(InTransmitterID);
			const FAudioRenderStepId BusStepId = FAudioRenderStepId::FromAudioBusKey(BusKey);
			SourceManager->GetRenderScheduler().AddDependency(BusStepId, SourceStepId);
		}
	}

	bool FMixerAudioBus::RemoveInstanceId(const int32 InSourceId, const uint64 InTransmitterID)
	{
		if (InstanceIds.Remove(InSourceId) > 0 && AudioMixerCVars::UseRenderScheduler)
		{
			const FAudioRenderStepId SourceStepId = FAudioRenderStepId::FromTransmitterID(InTransmitterID);
			const FAudioRenderStepId BusStepId = FAudioRenderStepId::FromAudioBusKey(BusKey);
			SourceManager->GetRenderScheduler().RemoveDependency(BusStepId, SourceStepId);
		}

		// Return true if there is no more instances or sends
		return bIsAutomatic && !InstanceIds.Num() && !AudioBusSends[(int32)EBusSendType::PreEffect].Num() && !AudioBusSends[(int32)EBusSendType::PostEffect].Num();
	}

	void FMixerAudioBus::AddSend(EBusSendType BusSendType, const FAudioBusSend& InAudioBusSend)
	{
		// Make sure we don't have duplicates in the bus sends
		for (FAudioBusSend& BusSend : AudioBusSends[(int32)BusSendType])
		{
			// If it's already added, just update the send level
			if (BusSend.SourceId == InAudioBusSend.SourceId)
			{
				BusSend.SendLevel = InAudioBusSend.SendLevel;
				return;
			}
		}

		// It's a new source id so just add it
		AudioBusSends[(int32)BusSendType].Add(InAudioBusSend);

		if (AudioMixerCVars::UseRenderScheduler)
		{
			// Make sure the source sending to the bus gets rendered before the bus is mixed
			const FAudioRenderStepId SourceStepId = FAudioRenderStepId::FromTransmitterID(InAudioBusSend.TransmitterID);
			const FAudioRenderStepId BusStepId = FAudioRenderStepId::FromAudioBusKey(BusKey);
			SourceManager->GetRenderScheduler().AddDependency(SourceStepId, BusStepId);
		}
	}

	bool FMixerAudioBus::RemoveSend(EBusSendType BusSendType, const int32 InSourceId)
	{
		TArray<FAudioBusSend>& Sends = AudioBusSends[(int32)BusSendType];

		for (int32 i = Sends.Num() - 1; i >= 0; --i)
		{
			// Remove this source id's send
			if (Sends[i].SourceId == InSourceId)
			{
				if (AudioMixerCVars::UseRenderScheduler)
				{
					const FAudioRenderStepId SourceStepId = FAudioRenderStepId::FromTransmitterID(Sends[i].TransmitterID);
					const FAudioRenderStepId BusStepId = FAudioRenderStepId::FromAudioBusKey(BusKey);
					SourceManager->GetRenderScheduler().RemoveDependency(SourceStepId, BusStepId);
				}

				Sends.RemoveAtSwap(i, EAllowShrinking::No);

				// There will only be one entry
				break;
			}
		}

		// Return true if there is no more instances or sends and this is an automatic audio bus
		return bIsAutomatic && !InstanceIds.Num() && !AudioBusSends[(int32)EBusSendType::PreEffect].Num() && !AudioBusSends[(int32)EBusSendType::PostEffect].Num();
	}

	void FMixerAudioBus::MixBuffer()
	{
		// Mix the patch mixer's inputs into the source data
		const int32 NumSamples = NumFrames * NumChannels;
		const int32 NumOutputFrames = SourceManager->GetNumOutputFrames();

		if (AudioMixerCVars::UseRenderScheduler)
		{
			CurrentBufferIndex = 1 - CurrentBufferIndex;
		}

		FAlignedFloatBuffer& MixBuffer = MixedSourceData[CurrentBufferIndex];
		float* BusDataBufferPtr = MixBuffer.GetData();

		PatchMixer.PopAudio(BusDataBufferPtr, NumSamples, false);

		for (int32 BusSendType = 0; BusSendType < (int32)EBusSendType::Count; ++BusSendType)
		{
			// Loop through the send list for this bus
			for (const FAudioBusSend& AudioBusSend : AudioBusSends[BusSendType])
			{
				const float* SourceBufferPtr = nullptr;

				// If the audio source mixing to this audio bus is itself a source bus, we need to use the previous renderer buffer to avoid infinite recursion.
				// With the render scheduler we don't need to treat source buses any differently from other sources -- the source bus should already have been
				//  rendered.
				if (!AudioMixerCVars::UseRenderScheduler && SourceManager->IsSourceBus(AudioBusSend.SourceId))
				{
					SourceBufferPtr = SourceManager->GetPreviousSourceBusBuffer(AudioBusSend.SourceId);
				}
				// If the source mixing into this is not itself a bus, then simply mix the pre-attenuation audio of the source into the bus
				// The source will have already computed its buffers for this frame
				else if (BusSendType == (int32)EBusSendType::PostEffect)
				{
					SourceBufferPtr = SourceManager->GetPreDistanceAttenuationBuffer(AudioBusSend.SourceId);
				}
				else
				{
					SourceBufferPtr = SourceManager->GetPreEffectBuffer(AudioBusSend.SourceId);
				}

				// It's possible we may not have a source buffer ptr here if the sound is not playing
				if (SourceBufferPtr)
				{
					const int32 NumSourceChannels = SourceManager->GetNumChannels(AudioBusSend.SourceId);
					const int32 NumSourceSamples = NumSourceChannels * NumOutputFrames;

					// Up-mix or down-mix if source channels differ from bus channels
					if (NumSourceChannels != NumChannels)
					{
						FAlignedFloatBuffer ChannelMap;
						SourceManager->Get2DChannelMap(AudioBusSend.SourceId, NumChannels, ChannelMap);
						Algo::ForEach(ChannelMap, [SendLevel = AudioBusSend.SendLevel](float& ChannelValue) { ChannelValue *= SendLevel; });
						DownmixAndSumIntoBuffer(NumSourceChannels, NumChannels, SourceBufferPtr, BusDataBufferPtr, NumOutputFrames, ChannelMap.GetData());
					}
					else
					{
						TArrayView<const float> SourceBufferView(SourceBufferPtr, NumOutputFrames * NumChannels);
						TArrayView<float> BusDataBufferView(BusDataBufferPtr, NumOutputFrames * NumChannels);
						ArrayMixIn(SourceBufferView, BusDataBufferView, AudioBusSend.SendLevel);
					}
				}
			}
		}

		// Send the mix to the patch splitter's outputs
		PatchSplitter.PushAudio(BusDataBufferPtr, NumSamples);

#if UE_AUDIO_PROFILERTRACE_ENABLED
		if (bIsEnvelopeFollowing)
		{
			ProcessEnvelopeFollower(BusDataBufferPtr);
		}
#endif // UE_AUDIO_PROFILERTRACE_ENABLED
	}

	void FMixerAudioBus::CopyCurrentBuffer(Audio::FAlignedFloatBuffer& InChannelMap, int32 InNumOutputChannels, FAlignedFloatBuffer& OutBuffer, int32 NumOutputFrames) const
	{
		check(NumChannels != InNumOutputChannels);
		DownmixAndSumIntoBuffer(NumChannels, InNumOutputChannels, MixedSourceData[CurrentBufferIndex], OutBuffer, InChannelMap.GetData());
	}

	void FMixerAudioBus::CopyCurrentBuffer(int32 InNumOutputChannels, FAlignedFloatBuffer& OutBuffer, int32 NumOutputFrames) const
	{
		const float* RESTRICT CurrentBuffer = GetCurrentBusBuffer();

		check(NumChannels == InNumOutputChannels);

		FMemory::Memcpy(OutBuffer.GetData(), CurrentBuffer, sizeof(float) * NumOutputFrames * InNumOutputChannels);
	}

	const float* FMixerAudioBus::GetCurrentBusBuffer() const
	{
		return MixedSourceData[CurrentBufferIndex].GetData();
	}

	const float* FMixerAudioBus::GetPreviousBusBuffer() const
	{
		return MixedSourceData[1 - CurrentBufferIndex].GetData();
	}

	void FMixerAudioBus::AddNewPatchOutput(const FPatchOutputStrongPtr& InPatchOutputStrongPtr)
	{
		PatchSplitter.AddNewPatch(InPatchOutputStrongPtr);
	}

	void FMixerAudioBus::AddNewPatchInput(const FPatchInput& InPatchInput)
	{
		return PatchMixer.AddNewInput(InPatchInput);
	}

	void FMixerAudioBus::RemovePatchInput(const FPatchInput& PatchInput)
	{
		return PatchMixer.RemovePatch(PatchInput);
	}


	void FMixerAudioBus::DoRenderStep()
	{
		MixBuffer();
	}

	const TCHAR* FMixerAudioBus::GetRenderStepName()
	{
		return TEXT("FMixerAudioBus mixing");
	}

#if UE_AUDIO_PROFILERTRACE_ENABLED
	void FMixerAudioBus::StartEnvelopeFollower(const float InAttackTime, const float InReleaseTime, const float InSampleRate)
	{
		if (!bIsEnvelopeFollowing)
		{
			FEnvelopeFollowerInitParams EnvelopeFollowerInitParams;

			EnvelopeFollowerInitParams.SampleRate = InSampleRate;
			EnvelopeFollowerInitParams.NumChannels = NumChannels;
			EnvelopeFollowerInitParams.AttackTimeMsec = InAttackTime;
			EnvelopeFollowerInitParams.ReleaseTimeMsec = InReleaseTime;

			EnvelopeFollower.Init(EnvelopeFollowerInitParams);

			// Zero out any previous envelope values which may have been in the array before starting up
			for (int32 ChannelIndex = 0; ChannelIndex < AUDIO_MIXER_MAX_OUTPUT_CHANNELS; ++ChannelIndex)
			{
				EnvelopeValues[ChannelIndex] = 0.0f;
			}

			bIsEnvelopeFollowing = true;
		}
	}

	void FMixerAudioBus::StopEnvelopeFollower()
	{
		bIsEnvelopeFollowing = false;
	}

	void FMixerAudioBus::ProcessEnvelopeFollower(const float* InBuffer)
	{
		FMemory::Memset(EnvelopeValues, sizeof(float) * AUDIO_MIXER_MAX_OUTPUT_CHANNELS);

		if (NumChannels > 0)
		{
			if (EnvelopeFollower.GetNumChannels() != NumChannels)
			{
				EnvelopeFollower.SetNumChannels(NumChannels);
			}

			EnvelopeFollower.ProcessAudio(InBuffer, NumFrames);

			const TArray<float>& EnvValues = EnvelopeFollower.GetEnvelopeValues();

			check(EnvValues.Num() == NumChannels);

			FMemory::Memcpy(EnvelopeValues, EnvValues.GetData(), sizeof(float) * NumChannels);

			Audio::ArrayClampInPlace(MakeArrayView(EnvelopeValues, NumChannels), 0.0f, 1.0f);
		}

		EnvelopeNumChannels = NumChannels;
	}
#endif // UE_AUDIO_PROFILERTRACE_ENABLED
}

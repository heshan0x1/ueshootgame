// Copyright Epic Games, Inc. All Rights Reserved.

#include "Audio/SimpleAudio.h"

#include "Audio/Encoders/Configs/AudioEncoderConfigAAC.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(SimpleAudio)

ESimpleAudioCodec USimpleAudioHelper::GuessCodec(TSharedRef<FAVInstance> const& Instance)
{
	if (Instance->Has<FAudioEncoderConfigAAC>())
	{
		return ESimpleAudioCodec::AAC;
	}

	return ESimpleAudioCodec::AAC;
}

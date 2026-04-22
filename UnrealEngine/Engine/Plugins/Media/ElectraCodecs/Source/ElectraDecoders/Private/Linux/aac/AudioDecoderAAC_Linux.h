// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IElectraDecoder.h"

#ifdef ELECTRA_DECODERS_ENABLE_LINUX

class IElectraAudioDecoderAAC_Linux : public IElectraDecoder
{
public:
	static void GetConfigurationOptions(TMap<FString, FVariant>& OutOptions);
	static TSharedPtr<IElectraDecoder, ESPMode::ThreadSafe> Create(const TMap<FString, FVariant>& InOptions);

	virtual ~IElectraAudioDecoderAAC_Linux() = default;
};

#endif

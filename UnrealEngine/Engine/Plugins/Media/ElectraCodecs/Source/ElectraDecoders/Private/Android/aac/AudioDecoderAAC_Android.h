// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once


#include "IElectraDecoder.h"

class IElectraAudioDecoderAAC_Android : public IElectraDecoder
{
public:
	static void GetConfigurationOptions(TMap<FString, FVariant>& OutOptions);
	static TSharedPtr<IElectraDecoder, ESPMode::ThreadSafe> Create(const TMap<FString, FVariant>& InOptions);

	virtual ~IElectraAudioDecoderAAC_Android() = default;
};

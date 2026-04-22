// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Pipeline/Node.h"
#include "Pipeline/PipelineData.h"
#include "AudioResampler.h"
#include "Sound/SoundWave.h"

#define UE_API METAHUMANPIPELINECORE_API

namespace UE::MetaHuman::Pipeline
{

class FAudioLoadNode : public FNode
{
public:

	UE_API FAudioLoadNode(const FString& InName);

	UE_API bool Load(const USoundWave* InSoundWave);

	UE_API virtual bool Start(const TSharedPtr<FPipelineData>& InPipelineData) override;
	UE_API virtual bool Process(const TSharedPtr<FPipelineData>& InPipelineData) override;
	UE_API virtual bool End(const TSharedPtr<FPipelineData>& InPipelineData) override;

	float FrameRate = 30;
	int32 FrameOffset = 0;

	enum ErrorCode
	{
		NoAudio = 0,
	};

private:

	int32 PcmIndex = 0;
	int32 StartFrame = -1;

	TArray<uint8> PcmData;
	uint16 NumChannels = 0;
	uint32 SampleRate = 0;
};

class FAudioSaveNode : public FNode
{
public:

	UE_API FAudioSaveNode(const FString& InName);

	UE_API virtual bool Process(const TSharedPtr<FPipelineData>& InPipelineData) override;
	UE_API virtual bool End(const TSharedPtr<FPipelineData>& InPipelineData) override;

	FString FilePath;

	enum ErrorCode
	{
		FailedToSave = 0,
	};

private:

	int32 NumChannels = 0;
	int32 SampleRate = 0;
	TArray<uint8> PcmData;
};

class FAudioConvertNode : public FNode
{
public:

	UE_API FAudioConvertNode(const FString& InName);

	UE_API virtual bool Start(const TSharedPtr<FPipelineData>& InPipelineData) override;
	UE_API virtual bool Process(const TSharedPtr<FPipelineData>& InPipelineData) override;

	int32 NumChannels = 0;
	int32 SampleRate = 0;

	enum ErrorCode
	{
		UnsupportedChannelMix = 0,
		FailedToResample,
	};

private:

	Audio::FResampler Resampler;
	bool bResamplerInitialized = false;
};

}

#undef UE_API

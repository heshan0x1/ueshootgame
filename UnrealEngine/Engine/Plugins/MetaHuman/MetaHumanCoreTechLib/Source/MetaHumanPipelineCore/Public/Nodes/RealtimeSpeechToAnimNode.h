// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Pipeline/Node.h"
#include "Pipeline/PipelineData.h"

#include "AudioDrivenAnimationMood.h"

#define UE_API METAHUMANPIPELINECORE_API

namespace UE::NNE
{
	class IModelInstanceGPU;
}

namespace UE::MetaHuman::Pipeline
{

class FRealtimeSpeechToAnimNode : public FNode
{
public:

	UE_API FRealtimeSpeechToAnimNode(const FString& InName);

	UE_API virtual bool Start(const TSharedPtr<FPipelineData>& InPipelineData) override;
	UE_API virtual bool Process(const TSharedPtr<FPipelineData>& InPipelineData) override;
	UE_API virtual bool End(const TSharedPtr<FPipelineData>& InPipelineData) override;

	UE_API bool LoadModels();

	UE_API void SetMood(EAudioDrivenAnimationMood InMood);
	UE_API EAudioDrivenAnimationMood GetMood(void);

	UE_API void SetMoodIntensity(float InMoodIntensity);
	UE_API float GetMoodIntensity(void);

	UE_API void SetLookahead(int32 InLookahead);
	UE_API int32 GetLookahead(void);

	enum ErrorCode
	{
		FailedToInitialize = 0,
		UnsupportedNumberOfChannels,
		UnsupportedSampleRate,
		FailedToRun,
	};

private:

	TSharedPtr<UE::NNE::IModelInstanceGPU> Model = nullptr;

	TArray<float> AudioBuffer;
	TArray<float> CurveValues;
	TArray<int64> Step;

	EAudioDrivenAnimationMood Mood = EAudioDrivenAnimationMood::Neutral;
	float MoodIntensity = 1.0;
	FCriticalSection MoodMutex;

	int32 Lookahead = 80;
	FCriticalSection LookaheadMutex;

	TArray<float> InputBuffer;
	TArray<float> FrameBuffer;

	FFrameAnimationData AnimOut;
};

}

#undef UE_API

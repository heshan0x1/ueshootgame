// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_EDITOR

#include "Pipeline/Node.h"
#include "Pipeline/PipelineData.h"
#include "DNAAsset.h"
#include "Speech2Face.h"

namespace UE::MetaHuman::Pipeline
{

class FSpeechToAnimNode : public FNode
{
public:

	METAHUMANPIPELINE_API FSpeechToAnimNode(const FString& InName);
	METAHUMANPIPELINE_API FSpeechToAnimNode(const FString& InTypeName, const FString& InName);
	METAHUMANPIPELINE_API virtual ~FSpeechToAnimNode() override;

	METAHUMANPIPELINE_API virtual bool LoadModels();
	METAHUMANPIPELINE_API bool LoadModels(const FAudioDrivenAnimationModels& InModels);
	METAHUMANPIPELINE_API void SetMood(const EAudioDrivenAnimationMood& InMood);
	METAHUMANPIPELINE_API void SetMoodIntensity(float InMoodIntensity);
	METAHUMANPIPELINE_API void SetOutputControls(const EAudioDrivenAnimationOutputControls& InOutputControls);

	METAHUMANPIPELINE_API virtual bool Start(const TSharedPtr<FPipelineData>& InPipelineData) override;
	METAHUMANPIPELINE_API virtual bool Process(const TSharedPtr<FPipelineData>& InPipelineData) override;
	METAHUMANPIPELINE_API virtual bool End(const TSharedPtr<FPipelineData>& InPipelineData) override;

	METAHUMANPIPELINE_API void CancelModelSolve();

	TWeakObjectPtr<class USoundWave> Audio = nullptr;
	bool bDownmixChannels = true;
	uint32 AudioChannelIndex = 0;
	uint32 ProcessingStartFrameOffset = 0; // Index of the first frame to arrive through pipeline
	float OffsetSec = 0; // When in audio to start solving
	float FrameRate = 0;
	bool bClampTongueInOut = true;
	bool bGenerateBlinks = true;

	enum ErrorCode
	{
		InvalidAudio = 0,
		InvalidChannelIndex,
		FailedToSolveSpeechToAnimation,
		FailedToInitialize,
		InvalidFrame,
		FailedToModifyUiControls,
		FailedToModifyRawControls
	};

protected:
	TUniquePtr<FSpeech2Face> Speech2Face = nullptr;

	TArray<TMap<FString, float>> Animation;
	TArray<TMap<FString, float>> HeadAnimation;
	bool bCancelStart = false;

private:
	METAHUMANPIPELINE_API virtual bool PreConversionModifyUiControls(TMap<FString, float>& InOutAnimationFrame, FString& OutErrorMsg);
	METAHUMANPIPELINE_API virtual bool PostConversionModifyRawControls(TMap<FString, float>& InOutAnimationFrame, FString& OutErrorMsg);
	METAHUMANPIPELINE_API void PrepareFromOutputControls();

	EAudioDrivenAnimationOutputControls OutputControls = EAudioDrivenAnimationOutputControls::FullFace;
	EAudioProcessingMode ProcessingMode = EAudioProcessingMode::Undefined;
	TSet<FString> ActiveRawControls;
};

}
#endif // WITH_EDITOR

// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Pipeline/Node.h"
#include "Pipeline/PipelineData.h"
#include "MetaHumanLocalLiveLinkSubject.h"



namespace UE::MetaHuman::Pipeline
{

class FAudioSourceNode : public FNode
{
public:

	FAudioSourceNode(const FString& InName);

	virtual bool Process(const TSharedPtr<FPipelineData>& InPipelineData) override;
	virtual bool End(const TSharedPtr<FPipelineData>& InPipelineData) override;

	enum ErrorCode
	{
		GeneralError = 0,
	};

	class FAudioSample
	{
	public:
		FAudioDataType Audio;
		FQualifiedFrameTime Time;
		FMetaHumanLocalLiveLinkSubject::ETimeSource TimeSource = FMetaHumanLocalLiveLinkSubject::ETimeSource::NotSet;
		int32 NumDropped = 0;
	};

	void AddAudioSample(FAudioSample&& InAudioSample);
	void SetError(const FString& InErrorMessage);

private:

	FCriticalSection Mutex;
	TArray<FAudioSample> AudioSamples;
	FString ErrorMessage;

	float FPS = -1;
	int32 FPSCount = 0;
	double FPSStart = 0;
};

}

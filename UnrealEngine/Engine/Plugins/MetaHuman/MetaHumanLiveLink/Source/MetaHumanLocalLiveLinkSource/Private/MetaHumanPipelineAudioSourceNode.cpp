// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanPipelineAudioSourceNode.h"



namespace UE::MetaHuman::Pipeline
{

FAudioSourceNode::FAudioSourceNode(const FString& InName) : FNode("AudioSource", InName)
{
	Pins.Add(FPin("Audio Out", EPinDirection::Output, EPinType::Audio));
	Pins.Add(FPin("Audio Sample Time Out", EPinDirection::Output, EPinType::QualifiedFrameTime));
	Pins.Add(FPin("Dropped Frame Count Out", EPinDirection::Output, EPinType::Int));
	Pins.Add(FPin("Audio Sample Time Source Out", EPinDirection::Output, EPinType::Int));
	Pins.Add(FPin("Capture FPS", EPinDirection::Output, EPinType::Float));
}

bool FAudioSourceNode::Process(const TSharedPtr<FPipelineData>& InPipelineData)
{
	FAudioDataType Audio;
	FQualifiedFrameTime AudioSampleTime;
	FMetaHumanLocalLiveLinkSubject::ETimeSource AudioSampleTimeSource = FMetaHumanLocalLiveLinkSubject::ETimeSource::NotSet;
	int32 NumDroppedFrames = 0;
	float FPSCopy = -1;

	int32 NumSamples = 0;

	while (NumSamples == 0)
	{ 
		if (*bAbort)
		{
			return false;
		}

		{
			FScopeLock Lock(&Mutex);

			NumSamples = AudioSamples.Num();
			FPSCopy = FPS;

			if (!ErrorMessage.IsEmpty())
			{
				InPipelineData->SetErrorNodeCode(ErrorCode::GeneralError);
				InPipelineData->SetErrorNodeMessage(ErrorMessage);

				return false;
			}
		}

		if (NumSamples == 0)
		{
			FPlatformProcess::Sleep(0.001f);
		}
	}

	if (NumSamples == 0)
	{
		return false;
	}
	else
	{
		FScopeLock Lock(&Mutex);

		for (int32 Index = 0; Index < NumSamples; ++Index)
		{
			NumDroppedFrames += AudioSamples[Index].NumDropped;
		}
		NumDroppedFrames += NumSamples - 1;

		Audio = MoveTemp(AudioSamples[NumSamples - 1].Audio);
		Audio.bContiguous = NumDroppedFrames == 0;
		AudioSampleTime = AudioSamples[NumSamples - 1].Time;
		AudioSampleTimeSource = AudioSamples[NumSamples - 1].TimeSource;

		AudioSamples.Reset();
	}

	InPipelineData->SetData<FAudioDataType>(Pins[0], MoveTemp(Audio));
	InPipelineData->SetData<FQualifiedFrameTime>(Pins[1], AudioSampleTime);
	InPipelineData->SetData<int32>(Pins[2], NumDroppedFrames);
	InPipelineData->SetData<int32>(Pins[3], static_cast<uint8>(AudioSampleTimeSource));
	InPipelineData->SetData<float>(Pins[4], FPSCopy);

	return true;
}

bool FAudioSourceNode::End(const TSharedPtr<FPipelineData>& InPipelineData)
{
	FScopeLock Lock(&Mutex);

	AudioSamples.Reset();
	ErrorMessage = "";

	return true;
}

void FAudioSourceNode::AddAudioSample(FAudioSample&& InAudioSample)
{
	double Now = FPlatformTime::Seconds();

	if (FPSCount == 0)
	{
		FPSStart = Now;
	}

	FPSCount += InAudioSample.NumDropped + 1;

	FScopeLock Lock(&Mutex);

	if (Now - FPSStart > 2)
	{
		FPS = (FPSCount - 1) / (Now - FPSStart);
		FPSCount = 0;
	}

	AudioSamples.Add(MoveTemp(InAudioSample));
}

void FAudioSourceNode::SetError(const FString& InErrorMessage)
{
	FScopeLock Lock(&Mutex);

	ErrorMessage = InErrorMessage;
}

}
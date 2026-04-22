// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanPipelineVideoSourceNode.h"



namespace UE::MetaHuman::Pipeline
{

FVideoSourceNode::FVideoSourceNode(const FString& InName) : FNode("VideoSource", InName)
{
	Pins.Add(FPin("UE Image Out", EPinDirection::Output, EPinType::UE_Image));
	Pins.Add(FPin("UE Image Sample Time Out", EPinDirection::Output, EPinType::QualifiedFrameTime));
	Pins.Add(FPin("Dropped Frame Count Out", EPinDirection::Output, EPinType::Int));
	Pins.Add(FPin("UE Image Sample Time Source Out", EPinDirection::Output, EPinType::Int));
	Pins.Add(FPin("Capture FPS", EPinDirection::Output, EPinType::Float));
}

bool FVideoSourceNode::Process(const TSharedPtr<FPipelineData>& InPipelineData)
{
	FUEImageDataType Image;
	FQualifiedFrameTime ImageSampleTime;
	FMetaHumanLocalLiveLinkSubject::ETimeSource ImageSampleTimeSource = FMetaHumanLocalLiveLinkSubject::ETimeSource::NotSet;
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

			NumSamples = VideoSamples.Num();
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
			NumDroppedFrames += VideoSamples[Index].NumDropped;
		}
		NumDroppedFrames += NumSamples - 1;

		Image = MoveTemp(VideoSamples[NumSamples - 1].Image);
		ImageSampleTime = VideoSamples[NumSamples - 1].Time;
		ImageSampleTimeSource = VideoSamples[NumSamples - 1].TimeSource;

		VideoSamples.Reset();
	}

	InPipelineData->SetData<FUEImageDataType>(Pins[0], MoveTemp(Image));
	InPipelineData->SetData<FQualifiedFrameTime>(Pins[1], ImageSampleTime);
	InPipelineData->SetData<int32>(Pins[2], NumDroppedFrames);
	InPipelineData->SetData<int32>(Pins[3], static_cast<uint8>(ImageSampleTimeSource));
	InPipelineData->SetData<float>(Pins[4], FPSCopy);

	return true;
}

bool FVideoSourceNode::End(const TSharedPtr<FPipelineData>& InPipelineData)
{
	FScopeLock Lock(&Mutex);

	VideoSamples.Reset();
	ErrorMessage = "";

	return true;
}

void FVideoSourceNode::AddVideoSample(FVideoSample&& InVideoSample)
{
	double Now = FPlatformTime::Seconds();

	if (FPSCount == 0)
	{
		FPSStart = Now;
	}

	FPSCount += InVideoSample.NumDropped + 1;

	FScopeLock Lock(&Mutex);

	if (Now - FPSStart > 2)
	{
		FPS = (FPSCount - 1) / (Now - FPSStart);
		FPSCount = 0;
	}

	VideoSamples.Add(MoveTemp(InVideoSample));
}

void FVideoSourceNode::SetError(const FString& InErrorMessage)
{
	FScopeLock Lock(&Mutex);

	ErrorMessage = InErrorMessage;
}

}
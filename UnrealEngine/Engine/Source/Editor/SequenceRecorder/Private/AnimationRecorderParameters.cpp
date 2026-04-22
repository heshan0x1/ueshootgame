// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimationRecorderParameters.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AnimationRecorderParameters)

float UAnimationRecordingParameters::GetRecordingDurationSeconds()
{
	return bEndAfterDuration ? MaximumDurationSeconds : FAnimationRecordingSettings::UnboundedMaximumLength;
}

const FFrameRate& UAnimationRecordingParameters::GetRecordingFrameRate()
{
	return SampleFrameRate;
}

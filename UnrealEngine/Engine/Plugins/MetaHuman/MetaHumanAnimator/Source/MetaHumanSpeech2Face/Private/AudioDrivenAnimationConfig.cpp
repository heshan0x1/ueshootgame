// Copyright Epic Games, Inc. All Rights Reserved.

#include "AudioDrivenAnimationConfig.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AudioDrivenAnimationConfig)

FAudioDrivenAnimationModels::FAudioDrivenAnimationModels()
{
	AudioEncoder = TEXT("/MetaHuman/Speech2Face/NNE_AudioDrivenAnimation_AudioEncoder.NNE_AudioDrivenAnimation_AudioEncoder");
	AnimationDecoder = TEXT("/MetaHuman/Speech2Face/NNE_AudioDrivenAnimation_AnimationDecoder.NNE_AudioDrivenAnimation_AnimationDecoder");
}

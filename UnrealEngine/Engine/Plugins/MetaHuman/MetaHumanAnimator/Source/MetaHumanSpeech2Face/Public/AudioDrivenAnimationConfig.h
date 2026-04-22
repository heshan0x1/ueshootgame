// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "FrameAnimationData.h"
#include "AudioDrivenAnimationMood.h"

#include "AudioDrivenAnimationConfig.generated.h"

#define UE_API METAHUMANSPEECH2FACE_API

USTRUCT(BlueprintType)
struct FAudioDrivenAnimationModels
{
	GENERATED_BODY()

	UE_API FAudioDrivenAnimationModels();

	// The model which will be used for audio encoding
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Properties, meta = (AllowedClasses = "/Script/NNE.NNEModelData"))
	FSoftObjectPath AudioEncoder;

	// The model which will be used for decoding the animation
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Properties, meta = (AllowedClasses = "/Script/NNE.NNEModelData"))
	FSoftObjectPath AnimationDecoder;
};

UENUM(BlueprintType)
enum class EAudioDrivenAnimationOutputControls : uint8
{
	FullFace UMETA(DisplayName = "Default (Full Face)"),
	MouthOnly,
};

USTRUCT(BlueprintType)
struct FAudioDrivenAnimationSolveOverrides
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Properties)
	EAudioDrivenAnimationMood Mood = EAudioDrivenAnimationMood::AutoDetect;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Properties, Meta = (UIMin = 0.0, ClampMin = 0.0, UIMax = 1.0, ClampMax = 1.0, Delta = 0.01))
	float MoodIntensity = 1.0;
};

#undef UE_API

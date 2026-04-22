// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/SCompoundWidget.h"

#if WITH_EDITOR
#include "PropertyHandle.h"
#endif

#define UE_API METAHUMANCORETECH_API

#include "AudioDrivenAnimationMood.generated.h"



UENUM(BlueprintType)
enum class EAudioDrivenAnimationMood : uint8
{
	AutoDetect = 255 UMETA(DisplayName = "Auto Detect"),

	Neutral = 0 UMETA(DisplayName = "Neutral"),
	Happiness = 1 UMETA(DisplayName = "Happy"),
	Sadness = 2 UMETA(DisplayName = "Sad"),
	Disgust = 3 UMETA(DisplayName = "Disgust"),
	Anger = 4 UMETA(DisplayName = "Anger"),
	Surprise = 5 UMETA(DisplayName = "Surprise"),
	Fear = 6 UMETA(DisplayName = "Fear"),
	Confidence = 10 UMETA(DisplayName = "Confident"),
	Excitement = 14 UMETA(DisplayName = "Excited"),
	Boredom = 15 UMETA(DisplayName = "Bored"),
	Playfulness = 17 UMETA(DisplayName = "Playful"),
	Confusion = 19 UMETA(DisplayName = "Confused"),
};

#if WITH_EDITOR
class SAudioDrivenAnimationMood : public SCompoundWidget
{
public:

	SLATE_BEGIN_ARGS(SAudioDrivenAnimationMood) {}
	SLATE_END_ARGS()

	UE_API void Construct(const FArguments& InArgs, bool bInOffline, TSharedRef<IPropertyHandle> InMoodPropertyHandle);
};
#endif

#undef UE_API

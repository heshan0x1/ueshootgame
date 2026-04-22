// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MetaHumanLocalLiveLinkSubjectSettings.h"
#include "Nodes/RealtimeSpeechToAnimNode.h"

#include "MetaHumanAudioBaseLiveLinkSubjectSettings.generated.h"



UCLASS(BlueprintType)
class METAHUMANLOCALLIVELINKSOURCE_API UMetaHumanAudioBaseLiveLinkSubjectSettings : public UMetaHumanLocalLiveLinkSubjectSettings
{
public:

	GENERATED_BODY()

	//~Begin UObject interface
#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& InPropertyChangedEvent) override;
#endif
	//~End UObject interface

	virtual void Setup() override;

	/* A very simplistic volume indicator to show if audio is being received - it is not a true audio level monitoring tool. */
	UPROPERTY(Transient, VisibleAnywhere, Category = "Audio", meta = (EditCondition = "bIsLiveProcessing", HideEditConditionToggle, EditConditionHides))
	float Level = 0;

	UPROPERTY(EditAnywhere, Category = "AudioControls")
	EAudioDrivenAnimationMood Mood = EAudioDrivenAnimationMood::Neutral;

	UFUNCTION(BlueprintCallable, Category = "MetaHuman Live Link")
	void SetMood(UPARAM(DisplayName = "Mood") EAudioDrivenAnimationMood InMood);

	UFUNCTION(BlueprintCallable, Category = "MetaHuman Live Link")
	void GetMood(UPARAM(DisplayName = "Mood") EAudioDrivenAnimationMood& OutMood) const;

	UPROPERTY(EditAnywhere, Category = "AudioControls", Meta = (UIMin = 0.0, ClampMin = 0.0, UIMax = 1.0, ClampMax = 1.0, Delta = 0.01))
	float MoodIntensity = 1.0;

	UFUNCTION(BlueprintCallable, Category = "MetaHuman Live Link")
	void SetMoodIntensity(UPARAM(DisplayName = "MoodIntensity") float InMoodIntensity);

	UFUNCTION(BlueprintCallable, Category = "MetaHuman Live Link")
	void GetMoodIntensity(UPARAM(DisplayName = "MoodIntensity") float& OutMoodIntensity) const;

	/* The amount of time, in milliseconds, that the audio solver looks ahead into the audio stream to produce the current frame of animation. A larger value will produce higher quality animation but will come at the cost of increased latency. */
	UPROPERTY(EditAnywhere, Category = "AudioControls", Meta = (UIMin = 80.0, ClampMin = 80.0, UIMax = 240.0, ClampMax = 240.0, Delta = 20.0))
	int32 Lookahead = 80.0;

	UFUNCTION(BlueprintCallable, Category = "MetaHuman Live Link")
	void SetLookahead(UPARAM(DisplayName = "Lookahead") int32 InLookahead);

	UFUNCTION(BlueprintCallable, Category = "MetaHuman Live Link")
	void GetLookahead(UPARAM(DisplayName = "Lookahead") int32& OutLookahead) const;
};

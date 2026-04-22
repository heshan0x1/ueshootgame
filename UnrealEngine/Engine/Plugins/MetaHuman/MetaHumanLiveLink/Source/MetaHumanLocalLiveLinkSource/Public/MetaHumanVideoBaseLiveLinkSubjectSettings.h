// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MetaHumanLocalLiveLinkSubjectSettings.h"

#include "Nodes/HyprsenseRealtimeNode.h"

#include "MetaHumanVideoBaseLiveLinkSubjectSettings.generated.h"



UENUM()
enum class EMetaHumanVideoRotation : uint8
{
	Zero = 0,
	Ninety,
	OneEighty,
	TwoSeventy,
};

UCLASS(BlueprintType)
class METAHUMANLOCALLIVELINKSOURCE_API UMetaHumanVideoBaseLiveLinkSubjectSettings : public UMetaHumanLocalLiveLinkSubjectSettings
{

public:

	GENERATED_BODY()

	UMetaHumanVideoBaseLiveLinkSubjectSettings();

	//~Begin UObject interface
#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& InPropertyChangedEvent) override;
#endif
	//~End UObject interface

	/* When enabled the rotational orientation of the head is output. You may want to disable this option if the head is being tracked by other means (eg mocap) or if you wish to analyze the facial animation on a static head. */
	UPROPERTY(EditAnywhere, Category = "Controls", meta = (EditCondition = "bIsLiveProcessing", HideEditConditionToggle, EditConditionHides))
	bool bHeadOrientation = true;

	UFUNCTION(BlueprintCallable, Category = "MetaHuman Live Link")
	void SetHeadOrientation(bool HeadOrientation);

	UFUNCTION(BlueprintCallable, Category = "MetaHuman Live Link")
	void GetHeadOrientation(bool& HeadOrientation) const;

	/* When enabled, and a neutral head position has been set, the position of the head is output. You may want to disable this option if the head is being tracked by other means (eg mocap) or if you wish to analyze the facial animation on a static head. */
	UPROPERTY(EditAnywhere, Category = "Controls", meta = (EditCondition = "bIsLiveProcessing", HideEditConditionToggle, EditConditionHides))
	bool bHeadTranslation = true;

	UFUNCTION(BlueprintCallable, Category = "MetaHuman Live Link")
	void SetHeadTranslation(bool HeadTranslation);

	UFUNCTION(BlueprintCallable, Category = "MetaHuman Live Link")
	void GetHeadTranslation(bool& HeadTranslation) const;

	/* Reduces noise in head position and orientation. */
	UPROPERTY(EditAnywhere, Category = "Controls", meta = (EditCondition = "bIsLiveProcessing", HideEditConditionToggle, EditConditionHides))
	bool bHeadStabilization = true;

	UFUNCTION(BlueprintCallable, Category = "MetaHuman Live Link")
	void SetHeadStabilization(bool HeadStabilization);

	UFUNCTION(BlueprintCallable, Category = "MetaHuman Live Link")
	void GetHeadStabilization(bool& HeadStabilization) const;

	/* Shows the video being processed. Options are None (no image), Input Video (the raw video), or Trackers (the video with tracking markers overlaid which can be useful in analysing the stability of the animation solve). Note this monitoring takes up resources so you may want to use it sparingly especially at high webcam frame rate or heavily loaded scenes */
	UPROPERTY(EditAnywhere, Category = "Image", meta = (EditCondition = "bIsLiveProcessing", HideEditConditionToggle, EditConditionHides))
	EHyprsenseRealtimeNodeDebugImage MonitorImage = EHyprsenseRealtimeNodeDebugImage::None;

	UFUNCTION(BlueprintCallable, Category = "MetaHuman Live Link")
	void SetMonitorImage(UPARAM(DisplayName = "MonitorImage") EHyprsenseRealtimeNodeDebugImage InMonitorImage);

	UFUNCTION(BlueprintCallable, Category = "MetaHuman Live Link")
	void GetMonitorImage(UPARAM(DisplayName = "MonitorImage") EHyprsenseRealtimeNodeDebugImage& OutMonitorImage) const;

	/* Size of the widget showing the video being processed. */
	UPROPERTY()
	FVector2D MonitorImageSize = FVector2D::ZeroVector;

	/* Allows for the input video to be rotated by 90, 180, or 270 degrees prior to processing. This can be used to account for different camera mountings. */
	UPROPERTY(EditAnywhere, Category = "Controls", DisplayName = "Image Rotation", meta = (EditCondition = "bIsLiveProcessing", HideEditConditionToggle, EditConditionHides))
	EMetaHumanVideoRotation Rotation = EMetaHumanVideoRotation::Zero;

	UFUNCTION(BlueprintCallable, Category = "MetaHuman Live Link")
	void SetRotation(UPARAM(DisplayName = "Rotation") EMetaHumanVideoRotation InRotation);

	UFUNCTION(BlueprintCallable, Category = "MetaHuman Live Link")
	void GetRotation(UPARAM(DisplayName = "Rotation") EMetaHumanVideoRotation& OutRotation) const;

	/* The focal length of the video being processed. */
	UPROPERTY(VisibleAnywhere, Category = "Controls", meta = (EditCondition = "bIsLiveProcessing", HideEditConditionToggle, EditConditionHides))
	double FocalLength = -1;

	virtual void CaptureNeutralHeadPose() override;

	/* A confidence value produced by the processing between 0 (poor) and 1 (good). */
	UPROPERTY(Transient)
	FString Confidence;

	/* The resolution of the video being processed. */
	UPROPERTY(Transient, VisibleAnywhere, Category = "Information", meta = (EditCondition = "bIsLiveProcessing", HideEditConditionToggle, EditConditionHides, DisplayPriority = "60"))
	FString Resolution;

	/* Whether video frames are being dropped because they can not be processed fast enough. */
	UPROPERTY(Transient, VisibleAnywhere, Category = "Information", meta = (EditCondition = "bIsLiveProcessing", HideEditConditionToggle, EditConditionHides, DisplayPriority = "70"))
	FString Dropping;
};
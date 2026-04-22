// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "LiveLinkHubSubjectSettings.h"

#include "MetaHumanRealtimeCalibration.h"
#include "MetaHumanRealtimeSmoothing.h"

#include "MetaHumanLiveLinkSubjectSettings.generated.h"



UCLASS(BlueprintType)
class METAHUMANLIVELINKSOURCE_API UMetaHumanLiveLinkSubjectSettings : public ULiveLinkHubSubjectSettings
{
public:

	GENERATED_BODY()

	UMetaHumanLiveLinkSubjectSettings();

	virtual void PostLoad() override;

#if WITH_EDITOR
	virtual void PostEditChangeChainProperty(struct FPropertyChangedChainEvent& PropertyChangedEvent) override;
#endif //WITH_EDITOR

	// The bIsLiveProcessing flag will be set to true when the settings are being used by a subject 
	// that is producing live data. This is the typical case, eg VideoSubjectSettings being used 
	// by a VideoSubject class.
	// 
	// The bIsLiveProcessing flag will be set to false when the settings are being used by a subject 
	// that is playing back pre-recorded data. This will be the case when using Take Recorder.
	// In this case we should hide all controls that would attempt to change the Live Link data being
	// produced, eg head translation on/off, since these will not apply to pre-recorded data.
	UPROPERTY(Transient)
	bool bIsLiveProcessing = false;

	UPROPERTY(EditAnywhere, Category = "Controls", meta = (EditCondition = "bIsLiveProcessing", HideEditConditionToggle, EditConditionHides))
	int32 CaptureNeutralsProperty = 0; // A dummy property thats customized to a button

	// Calibration
	UPROPERTY(EditAnywhere, Category = "Controls|Calibration", meta = (ToolTip = "The properties to calibrate.", EditCondition = "bIsLiveProcessing", HideEditConditionToggle, EditConditionHides))
	TArray<FName> Properties;

	UFUNCTION(BlueprintCallable, Category = "MetaHuman Live Link")
	void SetCalibrationProperties(UPARAM(DisplayName = "Properties") const TArray<FName>& InProperties);

	UFUNCTION(BlueprintCallable, Category = "MetaHuman Live Link")
	void GetCalibrationProperties(UPARAM(DisplayName = "Properties") TArray<FName>& OutProperties) const;

	UPROPERTY(EditAnywhere, Category = "Controls|Calibration", meta = (ClampMin = 0, ClampMax = 1, EditCondition = "bIsLiveProcessing", HideEditConditionToggle, EditConditionHides))
	float Alpha = 1.0;

	UFUNCTION(BlueprintCallable, Category = "MetaHuman Live Link")
	void SetCalibrationAlpha(UPARAM(DisplayName = "Alpha") float InAlpha);

	UFUNCTION(BlueprintCallable, Category = "MetaHuman Live Link")
	void GetCalibrationAlpha(UPARAM(DisplayName = "Alpha") float& OutAlpha) const;

	UPROPERTY(EditAnywhere, Category = "Controls|Calibration", meta = (EditCondition = "bIsLiveProcessing", HideEditConditionToggle, EditConditionHides))
	TArray<float> NeutralFrame;

	UFUNCTION(BlueprintCallable, Category = "MetaHuman Live Link")
	void SetCalibrationNeutralFrame(UPARAM(DisplayName = "NeutralFrame") const TArray<float>& InNeutralFrame);

	UFUNCTION(BlueprintCallable, Category = "MetaHuman Live Link")
	void GetCalibrationNeutralFrame(UPARAM(DisplayName = "NeutralFrame") TArray<float>& OutNeutralFrame) const;

	UPROPERTY(EditAnywhere, Category = "Controls|Calibration", meta = (EditCondition = "bIsLiveProcessing", HideEditConditionToggle, EditConditionHides))
	int32 CaptureNeutralFrameCountdown = -1;

	// Smoothing
	UPROPERTY(EditAnywhere, Category = "Controls|Smoothing", meta = (EditCondition = "bIsLiveProcessing", HideEditConditionToggle, EditConditionHides))
	TObjectPtr<UMetaHumanRealtimeSmoothingParams> Parameters;

	UFUNCTION(BlueprintCallable, Category = "MetaHuman Live Link")
	void SetSmoothing(UMetaHumanRealtimeSmoothingParams* Smoothing);

	UFUNCTION(BlueprintCallable, Category = "MetaHuman Live Link")
	void GetSmoothing(UMetaHumanRealtimeSmoothingParams*& Smoothing) const;

	// Head Pose (translation and orientation)

	// Head translation
	UPROPERTY(EditAnywhere, Category = "Controls|Head Pose", meta = (EditCondition = "bIsLiveProcessing", HideEditConditionToggle, EditConditionHides))
	FVector NeutralHeadTranslation = FVector::ZeroVector;

	UFUNCTION(BlueprintCallable, Category = "MetaHuman Live Link")
	void SetNeutralHeadTranslation(UPARAM(DisplayName = "NeutralHeadTranslation") const FVector& InNeutralHeadTranslation);

	UFUNCTION(BlueprintCallable, Category = "MetaHuman Live Link")
	void GetNeutralHeadTranslation(UPARAM(DisplayName = "NeutralHeadTranslation") FVector& OutNeutralHeadTranslation) const;

	// Head orientation
	UPROPERTY(EditAnywhere, Category = "Controls|Head Pose", meta = (EditCondition = "bIsLiveProcessing", HideEditConditionToggle, EditConditionHides))
	FRotator NeutralHeadOrientation = FRotator::ZeroRotator;

	UFUNCTION(BlueprintCallable, Category = "MetaHuman Live Link")
	void SetNeutralHeadOrientation(UPARAM(DisplayName = "NeutralHeadOrientation") const FRotator& InNeutralHeadOrientation);

	UFUNCTION(BlueprintCallable, Category = "MetaHuman Live Link")
	void GetNeutralHeadOrientation(UPARAM(DisplayName = "NeutralHeadOrientation") FRotator& OutNeutralHeadOrientation) const;

	UPROPERTY()
	FTransform NeutralHeadPoseInverse = FTransform::Identity;

	UPROPERTY(EditAnywhere, Category = "Controls|Head Pose", meta = (EditCondition = "bIsLiveProcessing", HideEditConditionToggle, EditConditionHides))
	int32 CaptureNeutralHeadPoseCountdown = -1;

	UFUNCTION(BlueprintCallable, Category = "MetaHuman Live Link")
	virtual void CaptureNeutrals();

	UFUNCTION(BlueprintCallable, Category = "MetaHuman Live Link")
	virtual void CaptureNeutralFrame();

	UFUNCTION(BlueprintCallable, Category = "MetaHuman Live Link")
	virtual void CaptureNeutralHeadPose();

	UE_DEPRECATED(5.7, "CaptureNeutralHeadTranslation() is deprecated. Use CaptureNeutralHeadPose() instead.")
	virtual void CaptureNeutralHeadTranslation() { CaptureNeutralHeadPose(); }

	UE_DEPRECATED(5.7, "CaptureNeutralHeadTranslationCountdown is deprecated. Use CaptureNeutralHeadPoseCountdown instead.")
	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "CaptureNeutralHeadTranslationCountdown is deprecated. Use CaptureNeutralHeadPoseCountdown instead."))
	int32 CaptureNeutralHeadTranslationCountdown = -1;

	bool PreProcess(const FLiveLinkBaseStaticData& InStaticData, FLiveLinkBaseFrameData& InOutFrameData);

private:

	TSharedPtr<FMetaHumanRealtimeCalibration> Calibration;

	TSharedPtr<FMetaHumanRealtimeSmoothing> Smoothing;
	double LastTime = 0;
};



UENUM(meta = (Bitmask, BitmaskEnum))
enum class EMetaHumanLiveLinkHeadPoseMode : uint8
{
	None = 0,
	CameraRelativeTranslation = 1 << 0,
	Orientation = 1 << 1,
};
ENUM_CLASS_FLAGS(EMetaHumanLiveLinkHeadPoseMode);

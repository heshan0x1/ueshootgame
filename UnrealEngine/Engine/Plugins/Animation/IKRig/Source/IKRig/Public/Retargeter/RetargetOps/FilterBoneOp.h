// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Animation/BoneReference.h"
#include "Retargeter/IKRetargetOps.h"
#include "Retargeter/IKRetargetProcessor.h"

#include "FilterBoneOp.generated.h"

#define UE_API IKRIG_API

#define LOCTEXT_NAMESPACE "FilterBoneOp"

struct FIKRetargetOpSettingsBase;

struct FOneEuroRotationFilter
{
	FQuat Update(
		const FQuat& InTargetRotation,
		double InDeltaTime,
		const FIKRetargetFilterBoneOpSettings& InSettings);

	void Reset() { bFirstRun = true; };

private:
	
	FQuat PrevFilteredRot = FQuat::Identity;
	FVector PrevHalfAngVel = FVector::ZeroVector;
	FVector PrevPhiFilt = FVector::ZeroVector;
	bool bFirstRun = true;
};

USTRUCT(BlueprintType)
struct FFilterBoneData
{
	GENERATED_BODY()

	// The target bone to filter.
	UPROPERTY(EditAnywhere, Category=Settings, meta=(ReinitializeOnEdit))
	FBoneReference TargetBone;

	bool bIsReady = false;
	FOneEuroRotationFilter Filter;
};

USTRUCT(BlueprintType, meta = (DisplayName = "Filter Bone Settings"))
struct FIKRetargetFilterBoneOpSettings : public FIKRetargetOpSettingsBase
{
	GENERATED_BODY()
	
	/** A list of bone-pairs to copy transforms between */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Setup, meta=(ReinitializeOnEdit))
	TArray<FFilterBoneData> BonesToFilter;

	/*/* When true, applies a One-Euro filter to smooth the rotation of the target bone #1#
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="One Euro Filter")
	bool bFilterRotation = true;*/

	/* Hz. Sets the low-pass cutoff when motion is near zero.
	 * Higher = less smoothing at rest (more responsive but more jitter).
	 * Lower = more smoothing at rest (less jitter but more “stickiness”). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="One Euro Filter")
	double MinFrequency = 1.5;

	/* Raises the cutoff as angular speed grows.
	 * Larger values are snappier on fast turns.
	 * Smaller values are smoother but laggier during quick motion.
	 * Typical sweet spot: 0.3 – 0.8 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="One Euro Filter")
	double Responsiveness = 0.5;

	/* Hz. Low-passes the raw angular velocity before we use it to adapt the derivative cutoff.
	* If you see breathing/pumping of the smoothing during motion onsets or reversals, lower this value (e.g., 30 → 20 Hz).
	* If responsiveness to fast changes is sluggish, raise this a bit.
	* Good starting range: 15–30 Hz. Keep under Nyquist frequency (frame_rate/2). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="One Euro Filter")
	double VelocityCutoffHz = 20.0;

	/* If true, filter is reset when playback loops. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="One Euro Filter")
	bool bResetPlayback = true;

	UE_API virtual const UClass* GetControllerType() const override;
	
	UE_API virtual void CopySettingsAtRuntime(const FIKRetargetOpSettingsBase* InSettingsToCopyFrom) override;
};

USTRUCT(BlueprintType, meta = (DisplayName = "Filter Bones"))
struct FIKRetargetFilterBoneOp : public FIKRetargetOpBase
{
	GENERATED_BODY()

	UE_API virtual bool Initialize(
		const FIKRetargetProcessor& Processor,
		const FRetargetSkeleton& SourceSkeleton,
		const FTargetSkeleton& TargetSkeleton,
		const FIKRetargetOpBase* InParentOp,
		FIKRigLogger& Log) override;
	
	UE_API virtual void Run(
		FIKRetargetProcessor& Processor,
		const double InDeltaTime,
		const TArray<FTransform>& InSourceGlobalPose,
		TArray<FTransform>& OutTargetGlobalPose) override;

	UE_API virtual FIKRetargetOpSettingsBase* GetSettings() override;
	
	UE_API virtual const UScriptStruct* GetSettingsType() const override;
	
	UE_API virtual const UScriptStruct* GetType() const override;

	UE_API virtual void OnPlaybackReset();
	
	UPROPERTY()
	FIKRetargetFilterBoneOpSettings Settings;

private:
	void ResetFilters(TArray<FTransform>& OutTargetGlobalPose);

	bool bResetPlayback = true;
};

/* The blueprint/python API for editing a Filter Bone Op */
UCLASS(MinimalAPI, BlueprintType)
class UIKRetargetFilterBoneController : public UIKRetargetOpControllerBase
{
	GENERATED_BODY()
	
public:
	/* Get the current op settings as a struct.
	 * @return FIKRetargetPinBoneOpSettings struct with the current settings used by the op. */
	UFUNCTION(BlueprintCallable, Category = Settings)
	UE_API FIKRetargetFilterBoneOpSettings GetSettings();

	/* Set the op settings. Input is a custom struct type for this op.
	 * @param InSettings a FIKRetargetPinBoneOpSettings struct containing all the settings to apply to this op */
	UFUNCTION(BlueprintCallable, Category = Settings)
	UE_API void SetSettings(FIKRetargetFilterBoneOpSettings InSettings);

	/* Clear all the bones */
	UFUNCTION(BlueprintCallable, Category = BonePairs)
	UE_API void ClearBonesToFilter();

	/* Add a target bone to filter. */
	UFUNCTION(BlueprintCallable, Category = BonePairs)
	UE_API void AddBoneToFilter(const FName InTargetBone);

	/* Get all the bones currently stored in the op.
	 * @return an array of target bone names*/
	UFUNCTION(BlueprintCallable, Category = BonePairs)
	UE_API TArray<FName> GetAllBonesToFilter();
};

#undef LOCTEXT_NAMESPACE

#undef UE_API

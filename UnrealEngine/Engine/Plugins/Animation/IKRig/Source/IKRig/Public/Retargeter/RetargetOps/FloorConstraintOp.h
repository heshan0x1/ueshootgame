// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Retargeter/IKRetargetChainMapping.h"
#include "Retargeter/IKRetargetOps.h"
#include "FloorConstraintOp.generated.h"

#define UE_API IKRIG_API

#define LOCTEXT_NAMESPACE "FloorConstraintOp"


USTRUCT(BlueprintType)
struct FFloorConstraintChainSettings
{
	GENERATED_BODY()

	FFloorConstraintChainSettings() = default;
	FFloorConstraintChainSettings(const FName InTargetChainName) : TargetChainName(InTargetChainName) {}

	/** The name of the TARGET chain to transfer animation onto. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Setup", meta=(ReinitializeOnEdit))
	FName TargetChainName;
	
	/** Whether to apply the floor constraint to the location of the IK goal on this chain. Default is false.
	 * When ON, the floor constraint will adjust the vertical position of the IK Goal according to the following rules.
	 * 1. When the source goal bone is LOWER than FloorHeightFalloffStart, the height of the goal is smoothly blended to the height of the source bone.
	 * 2. When the source goal bone is HIGHER than FloorHeightFalloffStart, the height of the goal is left at its normal retargeted location.
	 * NOTE: the floor is assumed to be the XY plane where Z = 0.
	 * NOTE: This only has an effect if the chain has an IK Goal assigned to it in the Target IK Rig asset.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Floor Constraint Chain Settings", meta=(ReinitializeOnEdit))
	bool EnableFloorConstraint = false;

	/** Range 0 to 1. Default is 0. Blend the effect of the constraint on this goal on/off. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Floor Constraint Chain Settings", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	double Alpha = 1.0f;

	/** Range 0 to 1. Default is 0. Maintain the height different between the source and target from the retarget pose. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Floor Constraint Chain Settings", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	double MaintainHeightOffset = 0.0f;

	UE_API bool operator==(const FFloorConstraintChainSettings& Other) const;

	FName GetName() const { return TargetChainName; };
	void SetName(const FName InName) { TargetChainName = InName; };
};

USTRUCT(BlueprintType, meta = (DisplayName = "Floor Goal Op Settings"))
struct FIKRetargetFloorConstraintOpSettings : public FIKRetargetOpSettingsBase
{
	GENERATED_BODY()

	/** The per-chain settings (exposed indirectly to the UI through a detail customization) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Chains, meta=(ReinitializeOnEdit))
	TArray<FFloorConstraintChainSettings> ChainsToAffect;

	/** Range 0 to inf. Default is 8. The height in cm from the floor below which the goal is snapped directly to the source bone height.
     * NOTE: if the source bone height is greater than this value, but lower than FloorHeightFalloffEnd, then the height will smoothly blend from the source
     * bone height, to the height of the goal in its normal retargeted position. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Floor Constraint Op Settings", meta = (ClampMin = "0.1", UIMin = "0.1", UIMax = "100.0"))
    double HeightFalloffOffset = 8.0f;
    
    /** Range 0 to inf. Default is 20. The height in cm from the floor below which the goal is gradually blended towards the source bone height.
     * NOTE: if the source bone is higher than this value, the height of the goal is left at its normal retargeted height. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Floor Constraint Op Settings", meta = (ClampMin = "0.1", UIMin = "0.1", UIMax = "100.0"))
    double HeightFalloffDistance = 20.0f;

	UE_API virtual const UClass* GetControllerType() const override;

	UE_API virtual void CopySettingsAtRuntime(const FIKRetargetOpSettingsBase* InSettingsToCopyFrom) override;
};

struct FFloorConstraint
{
	FName IKGoalName = NAME_None;
	int32 SourceEndBoneIndex = INDEX_NONE;
	double HeightOffsetInRefPose = 0.0;
	const FFloorConstraintChainSettings* Settings = nullptr;
};

USTRUCT(BlueprintType, meta = (DisplayName = "Floor Constraint"))
struct FIKRetargetFloorConstraintOp : public FIKRetargetOpBase
{
	GENERATED_BODY()
	
	UE_API virtual bool Initialize(
		const FIKRetargetProcessor& InProcessor,
		const FRetargetSkeleton& InSourceSkeleton,
		const FTargetSkeleton& InTargetSkeleton,
		const FIKRetargetOpBase* InParentOp,
		FIKRigLogger& InLog) override;
	
	UE_API virtual void Run(
		FIKRetargetProcessor& InProcessor,
		const double InDeltaTime,
		const TArray<FTransform>& InSourceGlobalPose,
		TArray<FTransform>& OutTargetGlobalPose) override;

	UE_API virtual void OnAddedToStack(const UIKRetargeter* InRetargetAsset, const FIKRetargetOpBase* InParentOp) override;

	UE_API virtual void OnAssignIKRig(const ERetargetSourceOrTarget SourceOrTarget, const UIKRigDefinition* InIKRig, const FIKRetargetOpBase* InParentOp) override;

	UE_API virtual FIKRetargetOpSettingsBase* GetSettings() override;
	
	UE_API virtual void SetSettings(const FIKRetargetOpSettingsBase* InSettings) override;
	
	UE_API virtual const UScriptStruct* GetSettingsType() const override;

	UE_API virtual const UScriptStruct* GetType() const override;

	UE_API virtual const UScriptStruct* GetParentOpType() const override;

	UE_API virtual void OnTargetChainRenamed(const FName InOldChainName, const FName InNewChainName) override;

	UE_API virtual void OnParentReinitPropertyEdited(const FIKRetargetOpBase& InParentOp, const FPropertyChangedEvent* InPropertyChangedEvent) override;

	UPROPERTY()
	FIKRetargetFloorConstraintOpSettings Settings;

#if WITH_EDITOR
	UE_API virtual void ResetChainSettingsToDefault(const FName& InChainName) override;
	UE_API virtual bool AreChainSettingsAtDefault(const FName& InChainName) override;
#endif

	TArray<FFloorConstraint> FloorConstraints;
};

/* The blueprint/python API for editing a Floor Goal Op */
UCLASS(MinimalAPI, BlueprintType)
class UIKRetargetFloorGoalsController : public UIKRetargetOpControllerBase
{
	GENERATED_BODY()
	
public:
	/* Get the current op settings as a struct.
	 * @return FIKRetargetIKChainsOpSettings struct with the current settings used by the op. */
	UFUNCTION(BlueprintCallable, Category = Settings)
	UE_API FIKRetargetIKChainsOpSettings GetSettings();

	/* Set the op settings. Input is a custom struct type for this op.
	 * @param InSettings a FIKRetargetIKChainsOpSettings struct containing all the settings to apply to this op */
	UFUNCTION(BlueprintCallable, Category = Settings)
	UE_API void SetSettings(FIKRetargetIKChainsOpSettings InSettings);
};

#undef LOCTEXT_NAMESPACE

#undef UE_API

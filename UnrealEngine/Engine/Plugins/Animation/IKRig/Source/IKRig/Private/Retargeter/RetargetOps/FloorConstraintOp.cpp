// Copyright Epic Games, Inc. All Rights Reserved.

#include "Retargeter/RetargetOps/FloorConstraintOp.h"

#include "Retargeter/IKRetargetOpUtils.h"
#include "Retargeter/IKRetargetProcessor.h"
#include "Retargeter/RetargetOps/IKChainsOp.h"
#include "Retargeter/RetargetOps/RunIKRigOp.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(FloorConstraintOp)

#define LOCTEXT_NAMESPACE "FloorConstraintOp"

bool FFloorConstraintChainSettings::operator==(const FFloorConstraintChainSettings& Other) const
{
	return EnableFloorConstraint == Other.EnableFloorConstraint
		&& FMath::IsNearlyEqualByULP(Alpha,Other.Alpha)
		&& FMath::IsNearlyEqualByULP(MaintainHeightOffset, Other.MaintainHeightOffset);
}

const UClass* FIKRetargetFloorConstraintOpSettings::GetControllerType() const
{
	return UIKRetargetFloorGoalsController::StaticClass();
}

void FIKRetargetFloorConstraintOpSettings::CopySettingsAtRuntime(const FIKRetargetOpSettingsBase* InSettingsToCopyFrom)
{
	// copies everything except the ChainsToAffect array (those are copied below, only for already existing chains)
	const TArray<FName> PropertiesToIgnore = {"ChainsToAffect"};
	FIKRetargetOpBase::CopyStructProperties(
		FIKRetargetFloorConstraintOpSettings::StaticStruct(),
		InSettingsToCopyFrom,
		this,
		PropertiesToIgnore);
	
	// copy settings only for chains that the op has initialized
	const FIKRetargetFloorConstraintOpSettings* NewSettings = reinterpret_cast<const FIKRetargetFloorConstraintOpSettings*>(InSettingsToCopyFrom);
	for (const FFloorConstraintChainSettings& NewChainSettings : NewSettings->ChainsToAffect)
	{
		for (FFloorConstraintChainSettings& ChainSettings : ChainsToAffect)
		{
			if (ChainSettings.TargetChainName == NewChainSettings.TargetChainName)
			{
				ChainSettings = NewChainSettings;
			}
		}
	}
}

bool FIKRetargetFloorConstraintOp::Initialize(
    const FIKRetargetProcessor& InProcessor,
    const FRetargetSkeleton& InSourceSkeleton,
    const FTargetSkeleton& InTargetSkeleton,
    const FIKRetargetOpBase* InParentOp,
    FIKRigLogger& InLog)
{
	bIsInitialized = false;
	
	// validate that an IK rig has been assigned to the parent op
	const FIKRetargetRunIKRigOp* ParentOp = reinterpret_cast<const FIKRetargetRunIKRigOp*>(InParentOp);
	if (ParentOp->Settings.IKRigAsset == nullptr)
	{
		InLog.LogWarning( FText::Format(
		LOCTEXT("MissingIKRig", "{0}, is missing an IK rig. No chains can be retargeted."), FText::FromName(GetName())));
		return false;
	}

	FloorConstraints.Reset();
	const FRetargeterBoneChains& BoneChains = InProcessor.GetBoneChains();
	for (const FFloorConstraintChainSettings& Chain : Settings.ChainsToAffect)
	{
		const FName TargetChainName = Chain.TargetChainName;
		const FResolvedBoneChain* TargetBoneChain = BoneChains.GetResolvedBoneChainByName(TargetChainName, ERetargetSourceOrTarget::Target,ParentOp->Settings.IKRigAsset);
		if (!TargetBoneChain)
		{
			InLog.LogWarning( FText::Format(
			LOCTEXT("FloorConstraintChainNotFound", "Floor Constraint: target chain not found in IK Rig, '{0}."), FText::FromName(TargetChainName)));
			continue;
		}

		if (TargetBoneChain->IKGoalName == NAME_None)
		{
			InLog.LogWarning( FText::Format(
			LOCTEXT("FloorConstraintIKNotFound", "Floor Constraint: target chain has no IK Goal, '{0}."), FText::FromName(TargetChainName)));
			continue;
		}

		// which source chain was this target chain mapped to?
		const FName SourceChainName = ParentOp->ChainMapping.GetChainMappedTo(TargetChainName, ERetargetSourceOrTarget::Target);
		const FResolvedBoneChain* SourceBoneChain = BoneChains.GetResolvedBoneChainByName(SourceChainName, ERetargetSourceOrTarget::Source);
		if (!SourceBoneChain)
		{
			InLog.LogWarning( FText::Format(
			LOCTEXT("FloorConstraintChainNotMapped", "Floor Constraint: found IK chain that was not mapped to a source chain, '{0}."), FText::FromName(TargetChainName)));
			continue;
		}

		// all prerequisites met, create a new floor constraint
		FFloorConstraint FloorConstraint;
		FloorConstraint.IKGoalName = TargetBoneChain->IKGoalName;
		FloorConstraint.SourceEndBoneIndex = SourceBoneChain->BoneIndices.Last();
		FloorConstraint.Settings = &Chain;

		// cache ref pose offset
		const FTransform& SourceEndBoneRefPose = SourceBoneChain->RefPoseGlobalTransforms.Last();
		const FTransform& TargetEndBoneRefPose = TargetBoneChain->RefPoseGlobalTransforms.Last();
		FloorConstraint.HeightOffsetInRefPose = TargetEndBoneRefPose.GetTranslation().Z - SourceEndBoneRefPose.GetTranslation().Z;

		// store floor constraint data for runtime
		FloorConstraints.Add(FloorConstraint);
	}
	
	bIsInitialized = true;
    return true;
}

void FIKRetargetFloorConstraintOp::Run(
    FIKRetargetProcessor& InProcessor,
    const double InDeltaTime,
    const TArray<FTransform>& InSourceGlobalPose,
    TArray<FTransform>& OutTargetGlobalPose)
{
	FIKRigGoalContainer& GoalContainer = InProcessor.GetIKRigGoalContainer();
	for (FFloorConstraint& FloorConstraint : FloorConstraints)
	{
		if (!FloorConstraint.Settings->EnableFloorConstraint)
		{
			continue; // constraint disabled
		}
		
		if (FMath::IsNearlyZero(FloorConstraint.Settings->Alpha))
		{
			continue; // constraint weight near zero
		}

		// get the goal to adjust
		FIKRigGoal* Goal = GoalContainer.FindGoalByName(FloorConstraint.IKGoalName);
		if (!ensure(Goal))
		{
			continue; // goal missing (should not happen at runtime)
		}

		// calculate falloff range
		const float FalloffStartHeight = Settings.HeightFalloffOffset;
		float FalloffEndHeight = FalloffStartHeight + FMath::Max(0.0, Settings.HeightFalloffDistance);

		// get the current height of the source bone
		const FTransform& CurrentSourceEndGlobal = InSourceGlobalPose[FloorConstraint.SourceEndBoneIndex];
		const float SourceHeight = FMath::Abs(CurrentSourceEndGlobal.GetLocation().Z);
		if (SourceHeight >= FalloffEndHeight)
		{
			continue; // constraint off while source is not near ground
		}
		
		// calculate the target height of the goal by blending towards the source height when close to floor
		const double FalloffRange = FMath::Max(1.0,FalloffEndHeight - FalloffStartHeight);
		const double Falloff = SourceHeight < FalloffStartHeight ? 1.0f : 1.0f - (SourceHeight - FalloffStartHeight) / FalloffRange;
		const double Weight = Falloff * FloorConstraint.Settings->Alpha;
		const double HeightOffset = FMath::Lerp(0.0f, FloorConstraint.HeightOffsetInRefPose, FloorConstraint.Settings->MaintainHeightOffset);
		const double TargetHeight = SourceHeight + HeightOffset;
		
		// lerp goal height from it's current height to the target height by the weight
		Goal->Position.Z = FMath::Lerp(Goal->Position.Z, TargetHeight, Weight);
	}
}

void FIKRetargetFloorConstraintOp::OnAddedToStack(const UIKRetargeter* InRetargetAsset, const FIKRetargetOpBase* InParentOp)
{
	// regenerate chain settings
	constexpr bool bSkipUnmappedChains = false;
	constexpr bool bSkipNonIKChains = true;
	IKRetargetOpUtils::SynchronizeChainSettingsWithIKRig(Settings.ChainsToAffect, InParentOp, bSkipUnmappedChains, bSkipNonIKChains);
}

void FIKRetargetFloorConstraintOp::OnAssignIKRig(
    const ERetargetSourceOrTarget SourceOrTarget,
    const UIKRigDefinition* InIKRig,
    const FIKRetargetOpBase* InParentOp)
{
	// regenerate chain settings
	constexpr bool bSkipUnmappedChains = false;
	constexpr bool bSkipNonIKChains = true;
	IKRetargetOpUtils::SynchronizeChainSettingsWithIKRig(Settings.ChainsToAffect, InParentOp, bSkipUnmappedChains, bSkipNonIKChains);
}

FIKRetargetOpSettingsBase* FIKRetargetFloorConstraintOp::GetSettings()
{
    return &Settings;
}

void FIKRetargetFloorConstraintOp::SetSettings(const FIKRetargetOpSettingsBase* InSettings)
{
    if (InSettings)
    {
        Settings = *static_cast<const FIKRetargetFloorConstraintOpSettings*>(InSettings);
    }
}

const UScriptStruct* FIKRetargetFloorConstraintOp::GetSettingsType() const
{
    return FIKRetargetFloorConstraintOpSettings::StaticStruct();
}

const UScriptStruct* FIKRetargetFloorConstraintOp::GetType() const
{
    return StaticStruct();
}

const UScriptStruct* FIKRetargetFloorConstraintOp::GetParentOpType() const
{
    return FIKRetargetRunIKRigOp::StaticStruct();
}

void FIKRetargetFloorConstraintOp::OnTargetChainRenamed(const FName InOldChainName, const FName InNewChainName)
{
	IKRetargetOpUtils::OnRetargetChainRenamed(Settings.ChainsToAffect, InOldChainName, InNewChainName);
}

void FIKRetargetFloorConstraintOp::OnParentReinitPropertyEdited(
    const FIKRetargetOpBase& InParentOp,
    const FPropertyChangedEvent* InPropertyChangedEvent)
{
	// regenerate chain settings
	constexpr bool bSkipUnmappedChains = false;
	constexpr bool bSkipNonIKChains = true;
	IKRetargetOpUtils::SynchronizeChainSettingsWithIKRig(Settings.ChainsToAffect, &InParentOp, bSkipUnmappedChains, bSkipNonIKChains);
}

// ------------------------------------------------------------
// Editor-only
// ------------------------------------------------------------
#if WITH_EDITOR

void FIKRetargetFloorConstraintOp::ResetChainSettingsToDefault(const FName& InChainName)
{
	for (FFloorConstraintChainSettings& Chain : Settings.ChainsToAffect)
	{
		if (Chain.TargetChainName == InChainName)
		{
			Chain = FFloorConstraintChainSettings(InChainName);
			return;
		}
	}
}

bool FIKRetargetFloorConstraintOp::AreChainSettingsAtDefault(const FName& InChainName)
{
	for (FFloorConstraintChainSettings& ChainToRetarget : Settings.ChainsToAffect)
	{
		if (ChainToRetarget.TargetChainName == InChainName)
		{
			FFloorConstraintChainSettings DefaultSettings = FFloorConstraintChainSettings();
			return ChainToRetarget == DefaultSettings;
		}
	}

	return true;
}

#endif // WITH_EDITOR

FIKRetargetIKChainsOpSettings UIKRetargetFloorGoalsController::GetSettings()
{
	return *reinterpret_cast<FIKRetargetIKChainsOpSettings*>(OpSettingsToControl);
}

void UIKRetargetFloorGoalsController::SetSettings(FIKRetargetIKChainsOpSettings InSettings)
{
	OpSettingsToControl->CopySettingsAtRuntime(&InSettings);
}

#undef LOCTEXT_NAMESPACE

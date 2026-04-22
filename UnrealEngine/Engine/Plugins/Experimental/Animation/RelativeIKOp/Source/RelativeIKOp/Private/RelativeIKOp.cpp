// Copyright Epic Games, Inc. All Rights Reserved.

#include "RelativeIKOp.h"

#include "IKRigObjectVersion.h"
#include "IKRigDebugRendering.h"
#include "RelativeBodyAnimNotifies.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Components/SkeletalMeshComponent.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/SkeletalBodySetup.h"
#include "Retargeter/IKRetargetProcessor.h"
#include "Retargeter/RetargetOps/RunIKRigOp.h"
#include "Rig/Solvers/PointsToRotation.h"

#if WITH_EDITOR
#include "PrimitiveDrawingUtils.h"
#include "Engine/EngineTypes.h"
#endif

#define LOCTEXT_NAMESPACE "RelativeIKOp"


const UClass* FIKRetargetRelativeIKOpSettings::GetControllerType() const
{
	return UIKRetargetRelativeIKController::StaticClass();
}

void FIKRetargetRelativeIKOpSettings::CopySettingsAtRuntime(const FIKRetargetOpSettingsBase* InSettingsToCopyFrom)
{
	static TArray<FName> PropertiesToIgnore = {"SourcePhysicsAssetOverride", "TargetPhysicsAssetOverride", "BodyMapping"};
	FIKRetargetOpBase::CopyStructProperties(
		FIKRetargetRelativeIKOpSettings::StaticStruct(),
		InSettingsToCopyFrom,
		this,
		PropertiesToIgnore);
}

bool FIKRetargetRelativeIKOp::Initialize(
	const FIKRetargetProcessor& InProcessor,
	const FRetargetSkeleton& InSourceSkeleton,
	const FTargetSkeleton& InTargetSkeleton,
	const FIKRetargetOpBase* InParentOp,
	FIKRigLogger& InLog)
{
	bIsInitialized = false;

	// this op requires a parent to supply an IK Rig
	if (!ensure(InParentOp))
	{
		return false;
	}

	// validate that an IK rig has been assigned
	const FIKRetargetRunIKRigOp* ParentRigOp = reinterpret_cast<const FIKRetargetRunIKRigOp*>(InParentOp);
	if (!ParentRigOp || ParentRigOp->Settings.IKRigAsset == nullptr)
	{
		InLog.LogWarning( FText::Format(
		LOCTEXT("MissingIKRig", "{0}, is missing an IK rig. No goals can be updated. "), FText::FromName(GetName())));
		return false;
	}

	SourcePhysicsAsset = Settings.SourcePhysicsAssetOverride;
	TargetPhysicsAsset = Settings.TargetPhysicsAssetOverride;

	if (!SourcePhysicsAsset || !TargetPhysicsAsset)
	{
		InLog.LogWarning( FText::Format(
		LOCTEXT("MissingPhysicsAssets", "{0}: Both Source and Target Physics Assets must be specified. "), FText::FromName(GetName())));
		return false;
	}

	UpdateCacheBoneChains(InProcessor, ParentRigOp);
	UpdateCacheSkelInfo(InSourceSkeleton, InTargetSkeleton);

	// Force update cache info if new anim montage found
	MontageInstance = nullptr;
	CacheSourceAnimSequence = nullptr;
	
	bIsInitialized = true;
	return bIsInitialized;
}

void FIKRetargetRelativeIKOp::Run(
	FIKRetargetProcessor& InProcessor,
	const double InDeltaTime,
	const TArray<FTransform>& InSourceGlobalPose,
	TArray<FTransform>& OutTargetGlobalPose)
{
#if WITH_EDITOR
	ResetDebugInfo();
#endif //WITH_EDITOR
	
	if (InProcessor.IsIKForcedOff())
	{
		return;
	}
	
	if (!bIsInitialized || !CacheSourceAnimSequence || !CachedNotifyInfo || AnimSeqPlayHead < 0.0f)
	{
		return;
	}
	
	TConstArrayView<FVector3f> FramePointView = ApplyTemporalSmoothing(AnimSeqPlayHead);

	FDebugRelativeIKDrawInfo LocalDebugInfo;
	
#if WITH_EDITOR
	LocalDebugInfo.SourcePairVerts.Reserve(CachedNotifyInfo->bBodyPairsIsParentDominates.Num());
	LocalDebugInfo.TargetPairVerts.Reserve(CachedNotifyInfo->bBodyPairsIsParentDominates.Num());

	TArray<FDebugRelativeTargetPairSpace> LocalTargetPairSpaces;
	LocalTargetPairSpaces.SetNumZeroed(CachedNotifyInfo->BodyPairs.Num());
#endif //WITH_EDITOR

	const double SourceScale = InProcessor.GetSourceScaleFactor();
	const double InvSourceScale = (Settings.bIgnoreSourceScale) ? 1.0 / InProcessor.GetSourceScaleFactor() : 1.0;

	// NOTE: Uniform scaling so these should compose fine w/ FTransform
	const FTransform SourceScaleTfm(FQuat::Identity, FVector::Zero(), FVector(SourceScale));
	const FTransform InvScaleTfm(FQuat::Identity, FVector::Zero(), FVector(InvSourceScale));
	
	FIKRigGoalContainer& GoalContainer = InProcessor.GetIKRigGoalContainer();
	TArray<FIKRigGoal>& IKGoals = GoalContainer.GetGoalArray();

	CacheChainLengthMap.Reset();
	CacheChainStartMap.Reset();
	
	const double RetargetContactAlpha = FMath::Clamp(Settings.RetargetContactAlpha, 0.0, 1.0);
	const double RetargetSpringAlpha = FMath::Clamp(Settings.RetargetSpringAlpha, 0.0, 1.0);
	// Get pair target body verts and weights
	for (int PairIdx = 0; PairIdx < CachedNotifyInfo->bBodyPairsIsParentDominates.Num(); ++PairIdx)
	{
		// TODO: Allow selecting a subset of body pairs in retargeter
		FrameBoneWeights[2*PairIdx] = 0;
		FrameBoneWeights[2*PairIdx + 1] = 0;
		FrameBoneTargets[2*PairIdx] = FVector::ZeroVector;
		FrameBoneTargets[2*PairIdx + 1] = FVector::ZeroVector;
		
		FName SourcePrimaryBone = CachedNotifyInfo->BodyPairs[2*PairIdx];
		FName SourceOtherBone = CachedNotifyInfo->BodyPairs[2*PairIdx + 1];

		FName TargetPrimaryBone = ApplyBodyMap(SourcePrimaryBone);
		FName TargetOtherBone = ApplyBodyMap(SourceOtherBone);

		if (TargetPrimaryBone == NAME_None || TargetOtherBone == NAME_None)
		{
			continue;
		}

		if (!CacheSourceSkelIndices.Contains(SourcePrimaryBone) || !CacheSourceSkelIndices.Contains(SourceOtherBone))
		{
			continue;
		}

		if (!CacheTargetSkelIndices.Contains(TargetPrimaryBone) || !CacheTargetSkelIndices.Contains(TargetOtherBone))
		{
			continue;
		}

		if (!HasBoneDelta(TargetPrimaryBone) || !HasBoneDelta(TargetOtherBone))
		{
			continue;
		}

		int32 SourcePrimaryPoseIdx = CacheSourceSkelIndices[SourcePrimaryBone];
		int32 SourceOtherPoseIdx = CacheSourceSkelIndices[SourceOtherBone];

		// ---- TESTING ONLY - Remove these once notify baking is rotation invariant
		FTransform SourcePrimaryBodyRot = GetBodyRotation(SourcePhysicsAsset, SourcePrimaryBone);
		FTransform SourceOtherBodyRot = GetBodyRotation(SourcePhysicsAsset, SourceOtherBone);
		FTransform TargetPrimaryBodyRot = SourcePrimaryBodyRot;
		FTransform TargetOtherBodyRot = SourceOtherBodyRot;
		
		// Compute source transforms and source-space vertex distance
		// const FVector3f& LocalPrimaryVert = CachedNotifyInfo->BodyPairsLocalReference[FramePairIdx + 2*PairIdx];
		// const FVector3f& LocalOtherVert = CachedNotifyInfo->BodyPairsLocalReference[FramePairIdx + 2*PairIdx + 1];
		const FVector3f& LocalPrimaryVert = FramePointView[2*PairIdx];
		const FVector3f& LocalOtherVert = FramePointView[2*PairIdx+1];

		// TODO: Clean this up if we bake in rot invariant space
		FVector SourceBoneBakePrimaryVert = (SourcePrimaryBodyRot).TransformPosition(FVector(LocalPrimaryVert)); 
		FVector SourceBoneBakeOtherVert = (SourceOtherBodyRot).TransformPosition(FVector(LocalOtherVert));
		// NOTE: Need to use FMatrix to apply oriented scale properly
		FMatrix SourceBodyPrimaryScale = GetBodyOrientedScale(SourcePhysicsAsset, SourcePrimaryBone);
		FMatrix SourceBodyOtherScale = GetBodyOrientedScale(SourcePhysicsAsset, SourceOtherBone);
		FVector SourceBodyPrimaryV = SourceBodyPrimaryScale.TransformPosition(SourceBoneBakePrimaryVert);
		FVector SourceBodyOtherV = SourceBodyOtherScale.TransformPosition(SourceBoneBakeOtherVert);
		
		FTransform SourcePrimaryBodyTfm = GetBodyTranslation(SourcePhysicsAsset, SourcePrimaryBone) * SourceScaleTfm;
		FTransform SourcePrimaryCompTfm = SourcePrimaryBodyTfm * InSourceGlobalPose[SourcePrimaryPoseIdx] * InvScaleTfm;
		FVector SourcePrimaryCompV = SourcePrimaryCompTfm.TransformPosition(SourceBodyPrimaryV);

		FTransform SourceOtherBodyTfm = GetBodyTranslation(SourcePhysicsAsset, SourceOtherBone) * SourceScaleTfm;
		FTransform SourceOtherCompTfm = SourceOtherBodyTfm * InSourceGlobalPose[SourceOtherPoseIdx] * InvScaleTfm;
		FVector SourceOtherCompV = SourceOtherCompTfm.TransformPosition(SourceBodyOtherV);

		double SourceVDist = FVector::Distance(SourcePrimaryCompV, SourceOtherCompV);
		double LinearWeight = FMath::Clamp(1.0 - (SourceVDist / Settings.DistanceThreshold), 0.0, 1.0);
		double Weight = GetDistanceWeight(SourceVDist, Settings.DistanceThreshold, Settings.DistanceFade);

		// Compute Source pair verts as represented in alternate pair body local space
		FVector SourcePrimaryOtherV = SourceBodyOtherScale.InverseTransformPosition(SourceOtherCompTfm.InverseTransformPosition(SourcePrimaryCompV));
		FVector SourceOtherPrimaryV = SourceBodyPrimaryScale.InverseTransformPosition(SourcePrimaryCompTfm.InverseTransformPosition(SourceOtherCompV));
		
		//---------------------------------
		// Get verts retargeted using physics body oriented scale
		int32 TargetPrimaryPoseIdx = CacheTargetSkelIndices[TargetPrimaryBone];
		int32 TargetOtherPoseIdx = CacheTargetSkelIndices[TargetOtherBone];

		FTransform TargetPrimaryBoneDelta = GetTargetBoneDelta(TargetPrimaryBone);
		FTransform TargetOtherBoneDelta = GetTargetBoneDelta(TargetOtherBone);
		
		FVector TargetBoneBakePrimaryVert = (TargetPrimaryBodyRot * TargetPrimaryBoneDelta).TransformPosition(FVector(LocalPrimaryVert)); 
		FVector TargetBoneBakeOtherVert = (TargetOtherBodyRot * TargetOtherBoneDelta).TransformPosition(FVector(LocalOtherVert));

		// NOTE: Need to use FMatrix to apply oriented scale properly
		FMatrix TargetBodyPrimaryScale = GetBodyOrientedScale(TargetPhysicsAsset, TargetPrimaryBone);
		FMatrix TargetBodyOtherScale = GetBodyOrientedScale(TargetPhysicsAsset, TargetOtherBone);
		FVector TargetBodyPrimaryV = TargetBodyPrimaryScale.TransformPosition(TargetBoneBakePrimaryVert);
		FVector TargetBodyOtherV = TargetBodyOtherScale.TransformPosition(TargetBoneBakeOtherVert);
		
		FTransform TargetPrimaryBodyTfm = GetBodyTranslation(TargetPhysicsAsset, TargetPrimaryBone);
		FTransform TargetOtherBodyTfm = GetBodyTranslation(TargetPhysicsAsset, TargetOtherBone);

		FTransform TargetPrimaryCompTfm = TargetPrimaryBodyTfm * OutTargetGlobalPose[TargetPrimaryPoseIdx];
		FTransform TargetOtherCompTfm = TargetOtherBodyTfm * OutTargetGlobalPose[TargetOtherPoseIdx];

		//TODO: Play with/think about offset strategy for these points
		FMatrix TestPrimaryScale = (Settings.bTestRetargetScale) ? TargetBodyPrimaryScale : SourceBodyPrimaryScale;
		FMatrix TestOtherScale = (Settings.bTestRetargetScale) ? TargetBodyOtherScale : SourceBodyOtherScale;
		FVector TargetPrimaryCompV_P = TargetPrimaryCompTfm.TransformPosition(TargetBodyPrimaryV);
		FVector TargetPrimaryCompV_O = TargetOtherCompTfm.TransformPosition(TestOtherScale.TransformPosition(TargetOtherBoneDelta.TransformPosition(SourcePrimaryOtherV)));
		FVector TargetPrimaryBoneOffset = OutTargetGlobalPose[TargetPrimaryPoseIdx].GetTranslation() - TargetPrimaryCompV_P;

		FVector TargetOtherCompV_O = TargetOtherCompTfm.TransformPosition(TargetBodyOtherV);
		FVector TargetOtherCompV_P = TargetPrimaryCompTfm.TransformPosition(TestPrimaryScale.TransformPosition(TargetPrimaryBoneDelta.TransformPosition(SourceOtherPrimaryV)));
		FVector TargetOtherBoneOffset = OutTargetGlobalPose[TargetOtherPoseIdx].GetTranslation() - TargetOtherCompV_O;

		// double SpringAlpha = (Settings.bTestDistContactAlpha) ? Weight : RetargetSpringAlpha;
		FVector TargetRelativeToPrimaryV = FMath::Lerp(TargetPrimaryCompV_P,TargetOtherCompV_P, RetargetSpringAlpha);
		FVector TargetRelativeToOtherV = FMath::Lerp(TargetPrimaryCompV_O,TargetOtherCompV_O, RetargetSpringAlpha);

		// double TargetVDist = FVector::Distance(TargetRelativeToPrimaryV, TargetRelativeToOtherV);
		// double TargetDRat = FMath::Min(1.0, 2.0 * FMath::Min(SourceVDist/TargetVDist, TargetVDist/SourceVDist));
		
		FVector RelativePrimaryOffset = TargetPrimaryCompV_P - TargetRelativeToPrimaryV;
		FVector RelativeOtherOffset = TargetOtherCompV_O - TargetRelativeToOtherV;

		// TODO: Should Eventually remove testing setup (keep dist weighting or drop it)
		bool bDistanceWeighting = CachedNotifyInfo->bBodyPairsIsParentDominates[PairIdx] && Settings.bTestDistContactAlpha;
		
		FDoubleInterval FeasibleRange(0.0, 1.0);
		UpdateCacheChainInfo(OutTargetGlobalPose, TargetOtherBone);
		if (bDistanceWeighting)
		{
			// TODO: Experiment with bi-directional distance weighting when Primary/Other can both move
			ComputeFeasibilityRange(FeasibleRange, TargetOtherBone, RelativeOtherOffset + TargetOtherBoneOffset, TargetRelativeToOtherV, TargetRelativeToPrimaryV);
		}

		double FeasibilityWeight = FMath::Lerp(FeasibleRange.Min, FeasibleRange.Max, LinearWeight);
		double ContactAlpha = (bDistanceWeighting) ? FeasibilityWeight : RetargetContactAlpha;
		FVector WeightedTarget = FMath::Lerp(TargetRelativeToOtherV, TargetRelativeToPrimaryV, ContactAlpha);

		FVector WeightedTargetPrimary = WeightedTarget + RelativePrimaryOffset;
		FVector WeightedTargetOther = WeightedTarget + RelativeOtherOffset;
		
		FrameBoneTargets[2*PairIdx] = WeightedTargetPrimary + TargetPrimaryBoneOffset;
		FrameBoneTargets[2*PairIdx + 1] = WeightedTargetOther + TargetOtherBoneOffset;

		FrameBoneWeights[2*PairIdx] = Weight;
		FrameBoneWeights[2*PairIdx + 1] = Weight;

#if WITH_EDITOR
		// Debug draw verts
		if (Settings.bDebugDraw)
		{
			LocalDebugInfo.SourcePairVerts.Add({SourcePrimaryCompV, SourceOtherCompV, false, FVector::ZeroVector, FVector::ZeroVector});
			if (Settings.DebugFullRetargetPairBones.Contains(TargetPrimaryBone))
			{
				LocalDebugInfo.TargetPairVerts.Add({TargetPrimaryCompV_P, TargetOtherCompV_O, true,TargetOtherCompV_P, TargetPrimaryCompV_O,});
			}
			else
			{
				LocalDebugInfo.TargetPairVerts.Add({TargetPrimaryCompV_P, TargetOtherCompV_O, false, FVector::ZeroVector, FVector::ZeroVector});
			}
			
			LocalTargetPairSpaces[2*PairIdx] = FDebugRelativeTargetPairSpace{WeightedTargetPrimary, TargetRelativeToOtherV+RelativePrimaryOffset, TargetRelativeToPrimaryV+RelativePrimaryOffset, FeasibleRange};
			LocalTargetPairSpaces[2*PairIdx+1] = FDebugRelativeTargetPairSpace{WeightedTargetOther, TargetRelativeToOtherV+RelativeOtherOffset, TargetRelativeToPrimaryV+RelativeOtherOffset, FeasibleRange};
		}
#endif //WITH_EDITOR
	}
	
	LocalDebugInfo.TargetGoals.Reserve(CacheSourceBodyEffectVertIdx.Num());
	// Update bone goals to relative IK positions
	for (int EffectIdx = 0; EffectIdx < CacheSourceBodyEffectVertIdx.Num(); ++ EffectIdx)
	{
		TArray<int32>& EffectIndices = CacheSourceBodyEffectVertIdx[EffectIdx];
		if (EffectIndices.IsEmpty())
		{
			continue;
		}

		FName SourceBoneName = CacheSourceEffectBones[EffectIdx];
		FName TargetBoneName = ApplyBodyMap(SourceBoneName);
		// HACK: Quick and dirty goal-by-bone find
		FIKRigGoal* IKGoal = IKGoals.FindByPredicate([TargetBoneName](const FIKRigGoal& Other){return Other.BoneName == TargetBoneName;});
		if (!IKGoal)
		{
			continue;
		}

		if (!CacheTargetSkelIndices.Contains(TargetBoneName))
		{
			continue;
		}

		double TotalWeight = 0;
		FVector GoalPoint = FVector::ZeroVector;

#if WITH_EDITOR
		if (Settings.bDebugDraw)
		{
			LocalDebugInfo.TargetGoals.AddDefaulted();
			LocalDebugInfo.TargetGoals.Last().Goal = FVector::ZeroVector;
		}
#endif //WITH_EDITOR

		double MaxWeight = 0;
		for (int Index : EffectIndices)
		{
			// if (MaxWeight < FrameBoneWeights[Index])
			// {
			// 	MaxWeight = FrameBoneWeights[Index];
			// 	GoalPoint = FrameBoneTargets[Index];
			// }
			GoalPoint += FrameBoneWeights[Index] * FrameBoneTargets[Index];
			TotalWeight += FrameBoneWeights[Index];

#if WITH_EDITOR
			if (Settings.bDebugDraw)
			{
				LocalDebugInfo.TargetGoals.Last().PairTargets.Add(FrameBoneTargets[Index]);
				LocalDebugInfo.PairRetargetInfo.Emplace(LocalTargetPairSpaces[Index]);
			}
#endif //WITH_EDITOR
		}

		
		if (!FMath::IsNearlyZero(TotalWeight))
		{
			GoalPoint /= TotalWeight;
#if WITH_EDITOR
			if (Settings.bDebugDraw)
			{
				LocalDebugInfo.TargetGoals.Last().Goal = GoalPoint;
			}
#endif // WITH_EDITOR
			
			// TODO: Play with total weight functions to alpha goal
			float GoalAlpha = FMath::Min(1.0f, static_cast<float>(TotalWeight*FMath::Clamp(Settings.ContributionSumWeight, 0.0f, 1.0f)));
			int32 TargetBoneIdx = CacheTargetSkelIndices[TargetBoneName];
			FVector PrevGoal = GoalLocBlendCompSpace(IKGoal, OutTargetGlobalPose[TargetBoneIdx]);
			FVector BlendGoal = FMath::Lerp(PrevGoal, GoalPoint, GoalAlpha);

			// TODO: Remove dry run for release
			bool bSetGoals = !Settings.bDebugDraw || !Settings.bDryRun;
			if (bSetGoals)
			{
				IKGoal->bEnabled = true;
				IKGoal->Position = BlendGoal;
				IKGoal->PositionAlpha = 1.0;
				IKGoal->PositionSpace = EIKRigGoalSpace::Component;
			}
		}
	}

#if WITH_EDITOR
	if (Settings.bDebugDraw)
	{
		FScopeLock ScopeLock(&DebugDataMutex);

		DebugDrawInfo.TargetGoals = LocalDebugInfo.TargetGoals;
		DebugDrawInfo.PairRetargetInfo = LocalDebugInfo.PairRetargetInfo;
		DebugDrawInfo.SourcePairVerts = LocalDebugInfo.SourcePairVerts;
		DebugDrawInfo.TargetPairVerts = LocalDebugInfo.TargetPairVerts;

		// Cache debug info for source bodies
		DebugDrawInfo.SourceBodyInfo.Reset(CacheSourceEffectBones.Num());
		for (FName SourceBoneName : CacheSourceEffectBones)
		{
			if (!CacheSourceSkelIndices.Contains(SourceBoneName))
			{
				continue;
			}

			int32 SourcePoseIdx = CacheSourceSkelIndices[SourceBoneName];
			DebugDrawInfo.SourceBodyInfo.Emplace(SourceBoneName, SourceScaleTfm * InSourceGlobalPose[SourcePoseIdx], FTransform::Identity);
		}

		// Cache debug info for target domain bodies
		DebugDrawInfo.TargetBodyInfo.Reset(CacheSourceDomainBones.Num());
		for (FName SourceBoneName : CacheSourceDomainBones)
		{
			FName TargetBoneName = ApplyBodyMap(SourceBoneName);
			if (!CacheTargetSkelIndices.Contains(TargetBoneName))
			{
				continue;
			}

			FTransform TargetBoneDelta = GetTargetBoneDelta(TargetBoneName);
			int32 TargetPoseIdx = CacheTargetSkelIndices[TargetBoneName];
			DebugDrawInfo.TargetBodyInfo.Emplace(TargetBoneName, OutTargetGlobalPose[TargetPoseIdx], TargetBoneDelta);
		}

		DebugDrawInfo.SourceTfmInfo.Reset(CacheSourceEffectBones.Num());
		for (FName SourceBoneName : CacheSourceEffectBones)
		{
			if (!CacheSourceSkelIndices.Contains(SourceBoneName))
			{
				continue;
			}

			int32 SourcePoseIdx = CacheSourceSkelIndices[SourceBoneName];

			// NOTE: Need to use FMatrix to apply oriented scale properly
			FMatrix SourceBodyRot = GetBodyRotation(SourcePhysicsAsset, SourceBoneName).ToMatrixNoScale();
			FMatrix SourceBodyScale = GetBodyOrientedScale(SourcePhysicsAsset, SourceBoneName);
			FMatrix SourceBodyTfm = SourceBodyRot * SourceBodyScale * GetBodyTranslation(SourcePhysicsAsset, SourceBoneName).ToMatrixNoScale();
			FMatrix SourceCompTfm = SourceBodyTfm * InSourceGlobalPose[SourcePoseIdx].ToMatrixWithScale();

			FVector SourceBodyCenter = InSourceGlobalPose[SourcePoseIdx].TransformPosition(GetBodyTranslation(SourcePhysicsAsset, SourceBoneName).GetLocation());
			FVector SourceBodyX = SourceCompTfm.TransformPosition(FVector::UnitX());
			FVector SourceBodyY = SourceCompTfm.TransformPosition(FVector::UnitY());
			FVector SourceBodyZ = SourceCompTfm.TransformPosition(FVector::UnitZ());
			
			DebugDrawInfo.SourceTfmInfo.Emplace(SourceBodyCenter, SourceBodyX, SourceBodyY, SourceBodyZ);
		}
		
		DebugDrawInfo.TargetTfmInfo.Reset(CacheSourceEffectBones.Num());
		for (FName SourceBoneName : CacheSourceEffectBones)
		{
			FName TargetBoneName = ApplyBodyMap(SourceBoneName);
			if (!CacheTargetSkelIndices.Contains(TargetBoneName))
			{
				continue;
			}
			
			int32 TargetPoseIdx = CacheTargetSkelIndices[TargetBoneName];

			// NOTE: Need to use FMatrix to apply oriented scale properly
			FMatrix TargetBoneDelta = GetTargetBoneDelta(TargetBoneName).ToMatrixNoScale();
			
			FMatrix SourceBodyRot = GetBodyRotation(SourcePhysicsAsset, SourceBoneName).ToMatrixNoScale();
			FMatrix TargetBodyScale = GetBodyOrientedScale(TargetPhysicsAsset, TargetBoneName);
			FMatrix TargetBodyTfm = SourceBodyRot * TargetBoneDelta * TargetBodyScale * GetBodyTranslation(TargetPhysicsAsset, TargetBoneName).ToMatrixNoScale();
			FMatrix TargetCompTfm = TargetBodyTfm * OutTargetGlobalPose[TargetPoseIdx].ToMatrixWithScale();

			FVector TargetBodyCenter = OutTargetGlobalPose[TargetPoseIdx].TransformPosition(GetBodyTranslation(TargetPhysicsAsset, TargetBoneName).GetLocation());
			FVector TargetBodyX = TargetCompTfm.TransformPosition(FVector::UnitX());
			FVector TargetBodyY = TargetCompTfm.TransformPosition(FVector::UnitY());
			FVector TargetBodyZ = TargetCompTfm.TransformPosition(FVector::UnitZ());
			
			DebugDrawInfo.TargetTfmInfo.Emplace(TargetBodyCenter, TargetBodyX, TargetBodyY, TargetBodyZ);
		}
	}
#endif
}

FName FIKRetargetRelativeIKOp::ApplyBodyMap(FName BodyName)
{
	if (Settings.BodyMapping.IsEmpty() || !Settings.BodyMapping.Contains(BodyName))
	{
		return BodyName;
	}

	return Settings.BodyMapping[BodyName];
}

bool FIKRetargetRelativeIKOp::HasBoneDelta(FName TargetBoneName) const
{
	return CacheMapBoneSourceTargetTfm.Contains(TargetBoneName);
}

const FTransform& FIKRetargetRelativeIKOp::GetTargetBoneDelta(FName TargetBoneName) const
{
	if (!CacheMapBoneSourceTargetTfm.Contains(TargetBoneName))
	{
		return FTransform::Identity;
	}
	return CacheMapBoneSourceTargetTfm[TargetBoneName];
}

void FIKRetargetRelativeIKOp::UpdateCacheChainInfo(const TArray<FTransform>& TargetPose, FName BoneName)
{
	if (!CacheBoneChains.Contains(BoneName))
	{
		return;
	}

	if (CacheChainLengthMap.Contains(BoneName))
	{
		return;
	}

	TArray<FTransform> ChainTransforms;
	ChainTransforms.Reserve(CacheBoneChains[BoneName]->BoneIndices.Num());
	for (int32 BoneIdx : CacheBoneChains[BoneName]->BoneIndices)
	{
		ChainTransforms.Add(TargetPose[BoneIdx]);
	}

	CacheChainLengthMap.Emplace(BoneName, CacheBoneChains[BoneName]->GetChainLength(ChainTransforms));
	CacheChainStartMap.Emplace(BoneName, TargetPose[CacheBoneChains[BoneName]->BoneIndices[0]].GetTranslation());
}

TConstArrayView<FVector3f> FIKRetargetRelativeIKOp::ApplyTemporalSmoothing(float Time)
{
	int32 NumBodies = CachedNotifyInfo->BodyPairs.Num();
	int32 SampleIdx = FMath::Clamp(static_cast<int32>(Time * SampleRate), 0, CachedNotifyInfo->NumSamples-1);

	// Testing temporal smoothing of local points
	const int32 MinSmoothIdx = FMath::Max(SampleIdx - Settings.TemporalSmoothingRadius, 0);
	const int32 MaxSmoothIdx = FMath::Min(SampleIdx + Settings.TemporalSmoothingRadius,CachedNotifyInfo->NumSamples-1) ;
	
	if (Settings.TemporalSmoothingRadius <= 0 || CachedNotifyInfo->NumSamples <= 0)
	{
		return {CachedNotifyInfo->BodyPairsLocalReference.GetData() + NumBodies * SampleIdx, NumBodies};
	}
	
	SmoothedPoints.SetNumUninitialized(NumBodies);
	for (int BodyIdx = 0; BodyIdx < CachedNotifyInfo->BodyPairs.Num(); ++BodyIdx)
	{
		SmoothedPoints[BodyIdx] = FVector3f::Zero();
		float SumWeight = 0.0f;
		for (int32 FrameIdx = MinSmoothIdx; FrameIdx <= MaxSmoothIdx; ++FrameIdx)
		{
			// Finite support Gaussian smoothing (could approximate w/ triangle or bake out smoothed data)
			const float Sigma = 0.25f * static_cast<float>(Settings.TemporalSmoothingRadius);
			const float SmoothDist = static_cast<float>(FrameIdx - SampleIdx);
			const float SmoothWeight = FMath::Exp(-0.5f*(SmoothDist*SmoothDist) / (Sigma*Sigma));
			SmoothedPoints[BodyIdx] += SmoothWeight * CachedNotifyInfo->BodyPairsLocalReference[NumBodies * FrameIdx + BodyIdx];
			SumWeight += SmoothWeight;
		}
		SmoothedPoints[BodyIdx] /= SumWeight;
	}
	return SmoothedPoints;
}


void FIKRetargetRelativeIKOp::ComputeFeasibilityRange(FDoubleInterval& OutFeasibleRange, FName BoneName, FVector BoneChainOffset, FVector TargetRelativeToBoneV, FVector TargetRelativeToOpposeBoneV)
{
	OutFeasibleRange = {0.0, 1.0};
	if (!Settings.bTestFeasibilityWeight)
	{
		return;
	}

	if (!CacheChainLengthMap.Contains(BoneName))
	{
		return;
	}
	
	FVector OffsetChainStart = CacheChainStartMap[BoneName] - BoneChainOffset;
	double CheckRadius = FMath::Max(CacheChainLengthMap[BoneName] + Settings.FeasibilityLengthBias, 0.0);
			
	double ChainDistStart = FVector::Distance(TargetRelativeToBoneV, OffsetChainStart);
	double ChainDistEnd = FVector::Distance(TargetRelativeToOpposeBoneV, OffsetChainStart);

	FDoubleInterval IntersectRange;
	if (FMath::Max(ChainDistStart, ChainDistEnd) <= CheckRadius)
	{
		// Whole range is feasible
		OutFeasibleRange = {0.0,1.0};
	}
	else if (!CalcLineSphereIntersect(IntersectRange, OffsetChainStart, CheckRadius, TargetRelativeToBoneV, TargetRelativeToOpposeBoneV))
	{
		// No feasible range, pick closer of the endpoints
		double RangeEndPt = (ChainDistStart > ChainDistEnd) ? 1.0 : 0.0;
		OutFeasibleRange = {RangeEndPt,RangeEndPt};
	}
	else
	{
		// Use sphere intersection range, clamped on [0,1]
		OutFeasibleRange = {FMath::Clamp(IntersectRange.Min, 0.0,1.0), FMath::Clamp(IntersectRange.Max, 0.0,1.0)};
	}
}

bool FIKRetargetRelativeIKOp::CalcLineSphereIntersect(FDoubleInterval& OutRange, const FVector& Center, double Radius, const FVector& StartPoint, const FVector& EndPoint)
{
	// NOTE: This function assumes two intersection points and returns valid positive ratios along line from start point, false if invalid/no intersection
	// Used for getting the endpoints of feasible range
	FVector LineVec = EndPoint - StartPoint;
	FVector LocPt = StartPoint - Center;

	double a = LineVec.SquaredLength();
	double b = FVector::DotProduct(LineVec, LocPt);
	double c = LocPt.SquaredLength() - Radius*Radius;
	if (b*b - a*c < 0.0 || LineVec.IsNearlyZero())
	{
		return false;
	}

	double B = FMath::Sqrt(b*b - 4.0*a*c);
	double MaxRange = (-b + B) / a;
	double MinRange = (-b - B) / a;
	OutRange = {MinRange, MaxRange};

	return true;
}

FTransform FIKRetargetRelativeIKOp::CalcReferenceShapeScale3D(FKShapeElem* ShapeElem)
{
	FTransform ElementScale3D = FTransform::Identity;
	switch (ShapeElem->GetShapeType())
	{
	case EAggCollisionShape::Sphere:
		{
			FKSphereElem* SphereElem = static_cast<FKSphereElem*>(ShapeElem);
			ElementScale3D.SetScale3D(FVector(SphereElem->Radius));
			break;
		}

	case EAggCollisionShape::Box:
		{
			FKBoxElem* BoxElem = static_cast<FKBoxElem*>(ShapeElem);
			ElementScale3D.SetScale3D(FVector(BoxElem->X, BoxElem->Y, BoxElem->Z));
			break;
		}

	case EAggCollisionShape::Sphyl:
		{
			FKSphylElem* CapsuleElem = static_cast<FKSphylElem*>(ShapeElem);
			ElementScale3D.SetScale3D(FVector(CapsuleElem->Radius,CapsuleElem->Radius,CapsuleElem->Length * 0.5f + CapsuleElem->Radius));
			break;
		}

	case EAggCollisionShape::Convex:
		{
			UE_LOG(LogAnimation, Warning, TEXT("Unsupported: Shape is a Convex"));
			break;
		}
	default:
		{
			UE_LOG(LogAnimation, Warning, TEXT("Unknown or unsupported shape type"));
			break;
		}
	}
	
	ensure(!FMath::IsNearlyZero(ElementScale3D.GetDeterminant()));
	
	return ElementScale3D;
}

FTransform FIKRetargetRelativeIKOp::GetBodyRotation(const UPhysicsAsset* PhysAsset, FName BoneName)
{
	FKShapeElem* ShapeElem = FindBodyShape(PhysAsset, BoneName);
	if (!ShapeElem)
	{
		return FTransform::Identity;
	}
	
	FTransform BodyTransform = ShapeElem->GetTransform();
	return FTransform(BodyTransform.GetRotation());
}

FTransform FIKRetargetRelativeIKOp::GetBodyTranslation(const UPhysicsAsset* PhysAsset, FName BoneName)
{
	FKShapeElem* ShapeElem = FindBodyShape(PhysAsset, BoneName);
	if (!ShapeElem)
	{
		return FTransform::Identity;
	}
	
	FTransform BodyTransform = ShapeElem->GetTransform();
	return FTransform(BodyTransform.GetTranslation());
}

FMatrix FIKRetargetRelativeIKOp::GetBodyOrientedScale(const UPhysicsAsset* PhysAsset, FName BoneName)
{
	FKShapeElem* ShapeElem = FindBodyShape(PhysAsset, BoneName);
	if (!ShapeElem)
	{
		return FMatrix::Identity;
	}
	
	FTransform BodyTransform = ShapeElem->GetTransform();
	FQuat BodyRotation = BodyTransform.GetRotation();
	FMatrix ElementScale3D = CalcReferenceShapeScale3D(ShapeElem).ToMatrixWithScale();
	return BodyRotation.Inverse().ToMatrix() * ElementScale3D * BodyRotation.ToMatrix();
}

FVector FIKRetargetRelativeIKOp::GoalLocBlendCompSpace(const FIKRigGoal* Goal, const FTransform& BoneTfm) const
{
	if (!Goal->bEnabled)
	{
		return BoneTfm.GetLocation();
	}

	switch (Goal->PositionSpace)
	{
		case EIKRigGoalSpace::Additive:
		{
			return FMath::Lerp(FVector::ZeroVector, Goal->Position, Goal->PositionAlpha) + BoneTfm.GetLocation();
		}
		case EIKRigGoalSpace::Component:
		{
			return FMath::Lerp(BoneTfm.GetLocation(), Goal->Position, Goal->PositionAlpha);
		}
		case EIKRigGoalSpace::World:
		default:
		{
			// We assume no World-space goals will be set using retarget stack
		}
	}

	return BoneTfm.GetLocation();
}

FKShapeElem* FIKRetargetRelativeIKOp::FindBodyShape(const UPhysicsAsset* PhysAsset, FName BoneName)
{
	int32 BodyIdx = PhysAsset->FindBodyIndex(BoneName);
	if (BodyIdx == INDEX_NONE)
	{
		UE_LOG(LogAnimation, Warning, TEXT("No body index found: %s"), *BoneName.ToString());
		return nullptr;
	}
	return PhysAsset->SkeletalBodySetups[BodyIdx]->AggGeom.GetElement(0);
}

double FIKRetargetRelativeIKOp::GetDistanceWeight(double Distance, double DistThreshold, double ScalarFade)
{
	double Barrier = -(DistThreshold-Distance)*FMath::Max(DistThreshold-Distance, 0.0f) * FMath::Log2(Distance / DistThreshold);
	return Barrier / (Barrier + ScalarFade); 
}

void FIKRetargetRelativeIKOp::AnimGraphPreUpdateMainThread(USkeletalMeshComponent& SourceMeshComponent, USkeletalMeshComponent& TargetMeshComponent)
{
	if (!bIsInitialized)
	{
		return;
	}
	
	const FAnimMontageInstance* PrevMontageInstance = MontageInstance;
	MontageInstance = nullptr;

	UAnimInstance* SourceAnimInstance = SourceMeshComponent.GetAnimInstance();
	if (!SourceAnimInstance)
	{
		return;
	}
	
	MontageInstance = SourceAnimInstance->GetActiveMontageInstance();
	if (MontageInstance)
	{
		// Early-out for montages
		if (MontageInstance == PrevMontageInstance)
		{
			PreUpdateMontagePlayhead();
			return;
		}

		ResetCacheNotifyInfo();
		SetupRelativeIKNotifyInfoMontage();
		return;
	}

	UpdateRelativeIKNotifyInfoAnimSeq(SourceAnimInstance);
}

void FIKRetargetRelativeIKOp::UpdateCacheBoneChains(const FIKRetargetProcessor& InProcessor, const FIKRetargetRunIKRigOp* ParentRigOp)
{
	CacheBoneChains.Reset();

	// TODO: This is a reinit case from parent (need to handle appropriately)
	// Cache resolved chains in target map by goal bone
	TArray<FName> RequiredChains = ParentRigOp->GetRequiredTargetChains();
	const FRetargeterBoneChains& RetargetBoneChains = InProcessor.GetBoneChains();
	
	for (FName Chain : RequiredChains)
	{
		const FResolvedBoneChain* BoneChain = RetargetBoneChains.GetResolvedBoneChainByName(Chain, ERetargetSourceOrTarget::Target, ParentRigOp->Settings.IKRigAsset);
		if (!BoneChain)
		{
			continue;
		}

		CacheBoneChains.Emplace(BoneChain->EndBone, BoneChain);
	}
}

void FIKRetargetRelativeIKOp::UpdateSourceBoneMap(FName SourceBoneName)
{
	int32 SourceIdx = SourceBoneNames.Find(SourceBoneName);
	if (SourceIdx == INDEX_NONE)
	{
		CacheSourceSkelIndices.Remove(SourceBoneName);
		return;
	}

	CacheSourceSkelIndices.Emplace(SourceBoneName, SourceIdx);
}

void FIKRetargetRelativeIKOp::UpdateTargetBoneMap(FName TargetBoneName)
{
	int32 TargetIdx = TargetBoneNames.Find(TargetBoneName);
	if (TargetIdx == INDEX_NONE)
	{
		CacheTargetSkelIndices.Remove(TargetBoneName);
		return;
	}
	
	CacheTargetSkelIndices.Emplace(TargetBoneName, TargetIdx);
}

void FIKRetargetRelativeIKOp::UpdateBoneMapTfm(FName SourceBoneName, FName TargetBoneName)
{
	int32 SourceIdx = SourceBoneNames.Find(SourceBoneName);
	int32 TargetIdx = TargetBoneNames.Find(TargetBoneName);
	if (SourceIdx == INDEX_NONE || TargetIdx == INDEX_NONE)
	{
		CacheMapBoneSourceTargetTfm.Remove(TargetBoneName);
		return;
	}

	FQuat SourceRot = CacheSourceBoneInitTfm[SourceIdx].GetRotation();
	FQuat TargetInvRot = CacheTargetBoneInitTfm[TargetIdx].GetRotation().Inverse();

	FTransform TargetDelta = FTransform(TargetInvRot*SourceRot);
	CacheMapBoneSourceTargetTfm.Emplace(TargetBoneName,TargetDelta);
}

void FIKRetargetRelativeIKOp::UpdateCacheSkelInfo(const FRetargetSkeleton& InSourceSkeleton, const FTargetSkeleton& InTargetSkeleton)
{
	SourceBoneNames = InSourceSkeleton.BoneNames;
	TargetBoneNames = InTargetSkeleton.BoneNames;

	CacheSourceBoneInitTfm = InSourceSkeleton.RetargetPoses.GetGlobalRetargetPose();
	CacheTargetBoneInitTfm = InTargetSkeleton.RetargetPoses.GetGlobalRetargetPose();

	CacheSourceSkelIndices.Reset();
	CacheTargetSkelIndices.Reset();
}

void FIKRetargetRelativeIKOp::PreUpdateMontagePlayhead()
{
	if (!MontageInstance)
	{
		AnimSeqPlayHead = -1.0f;
		return;
	}

	// TODO: DeltaTimeRecord doesn't seem to work for montages when scrubbing
	float MontageTime = MontageInstance->GetPosition();
	AnimSeqPlayHead = MontageTime - SegmentStartTime;
	if (MontageTime > SegmentEndTime)
	{
		AnimSeqPlayHead = -1.0f;
	}
}

void FIKRetargetRelativeIKOp::SetupRelativeIKNotifyInfoMontage()
{
	CacheSourceAnimSequence = nullptr;
	UAnimMontage* Montage = MontageInstance->Montage;
	if (!Montage)
	{
		return;
	}

	// HACK: Only grabs first relative notify
	for (const FSlotAnimationTrack& SlotTrack : Montage->SlotAnimTracks)
	{
		for (const FAnimSegment& Segment : SlotTrack.AnimTrack.AnimSegments)
		{
			const TObjectPtr<UAnimSequenceBase>& SeqRef = Segment.GetAnimReference();
			CacheSourceAnimSequence = Cast<UAnimSequence>(SeqRef.Get());
			if (!CacheSourceAnimSequence)
			{
				continue;
			}
			
			for (const FAnimNotifyEvent& NotifyEvent : SeqRef->Notifies)
			{
				URelativeBodyBakeAnimNotify* BakeNotifyInfo = Cast<URelativeBodyBakeAnimNotify>(NotifyEvent.Notify);
				if (BakeNotifyInfo)
				{
					SegmentStartTime = Segment.AnimStartTime;
					SegmentEndTime = Segment.AnimEndTime;
					PreUpdateMontagePlayhead();
					UpdateCacheNotifyInfo(BakeNotifyInfo);
					return;
				}
			}
		}
	}
}

void FIKRetargetRelativeIKOp::UpdateRelativeIKNotifyInfoAnimSeq(UAnimInstance* SourceAnimInstance)
{
	// Get source tick records
	TMap<FName, FAnimGroupInstance> SyncGroupTickRecords = SourceAnimInstance->GetSyncGroupMapRead();
	TArray<FAnimTickRecord> UngroupedTickRecords = SourceAnimInstance->GetUngroupedActivePlayersRead();

	// Check for active players with baked notify data, keep hightest blend amount
	float MaxBlend = -1.0f;
	const FAnimTickRecord* MaxNotifyRecord = nullptr;
	auto ProcessTickRecords = [&MaxBlend,&MaxNotifyRecord](const TArray<FAnimTickRecord>& ActivePlayers)
		{
			for (const FAnimTickRecord& ActivePlayer : ActivePlayers)
			{
				if ( ActivePlayer.EffectiveBlendWeight <= MaxBlend )
				{
					continue;
				}
				if (UAnimSequence* Sequence = Cast<UAnimSequence>(ActivePlayer.SourceAsset))
				{
					// Used to determine which notifies should we considered "active".
					check(ActivePlayer.DeltaTimeRecord->IsPreviousValid())
					for (const FAnimNotifyEvent& NotifyEvent : Sequence->Notifies)
					{
						URelativeBodyBakeAnimNotify* BakedNotifyInfo = Cast<URelativeBodyBakeAnimNotify>(NotifyEvent.Notify);
						if (BakedNotifyInfo)
						{
							MaxBlend = ActivePlayer.EffectiveBlendWeight;
							MaxNotifyRecord = &ActivePlayer;
						}
					}
				}
			}
		};
	
	// Find all the anim sequences (with sync groups) we updated this tick.
	for (const TTuple<FName, FAnimGroupInstance>& SyncGroupPair : SyncGroupTickRecords)
	{
		ProcessTickRecords(SyncGroupPair.Value.ActivePlayers);
	}
	
	// Find all the anim sequences (no sync groups) we updated this tick.
	ProcessTickRecords(UngroupedTickRecords);
	
	if (!MaxNotifyRecord)
	{
		CacheSourceAnimSequence = nullptr;
		ResetCacheNotifyInfo();
		return;
	}

	AnimSeqPlayHead = MaxNotifyRecord->DeltaTimeRecord->GetPrevious() + MaxNotifyRecord->DeltaTimeRecord->Delta;
	if (MaxNotifyRecord->SourceAsset == CacheSourceAnimSequence)
	{
		return;
	}
	
	CacheSourceAnimSequence = Cast<UAnimSequence>(MaxNotifyRecord->SourceAsset);

	// NOTE: Only grabs first bake notify
	ResetCacheNotifyInfo();
	for (const FAnimNotifyEvent& NotifyEvent : CacheSourceAnimSequence->Notifies)
	{
		URelativeBodyBakeAnimNotify* BakedNotifyInfo = Cast<URelativeBodyBakeAnimNotify>(NotifyEvent.Notify);
		if (BakedNotifyInfo)
		{
			UpdateCacheNotifyInfo(BakedNotifyInfo);
			return;
		}
	}
}


void FIKRetargetRelativeIKOp::ResetCacheNotifyInfo()
{
	SegmentStartTime = -1.0f;
	SegmentEndTime = -1.0f;
	CachedNotifyInfo = nullptr;

	// TODO: Need to bake info in accessible bipartite graph structure and cache less data!
	// Clear all cached data on update
	CacheMapBoneSourceTargetTfm.Reset();
	CacheSourceBodyEffectVertIdx.Reset();
	CacheSourceEffectBones.Reset();
	CacheSourceDomainBones.Reset();
	FrameBoneTargets.Reset();
	FrameBoneWeights.Reset();
}

void FIKRetargetRelativeIKOp::UpdateCacheNotifyInfo(URelativeBodyBakeAnimNotify* NotifyInfo)
{
	CachedNotifyInfo = NotifyInfo;

	int32 NumBodies = CachedNotifyInfo->BodyPairs.Num();
	FrameBoneTargets.SetNum(NumBodies);
	FrameBoneWeights.SetNumZeroed(NumBodies);

	SampleRate = FMath::RoundToFloat(1.0f / (CachedNotifyInfo->BodyPairsSampleTime[1] - CachedNotifyInfo->BodyPairsSampleTime[0]));

	TMap<FName,int32> CheckSourceBonesMap;
	for (int BodyIdx = 0; BodyIdx < NumBodies; ++BodyIdx)
	{
		FName SourceBodyBone = CachedNotifyInfo->BodyPairs[BodyIdx];
		if (CheckSourceBonesMap.Contains(SourceBodyBone))
		{
			continue;
		}

		CheckSourceBonesMap.Emplace(SourceBodyBone, CacheSourceEffectBones.Num());
		CacheSourceEffectBones.Add(SourceBodyBone);
		CacheSourceBodyEffectVertIdx.AddDefaulted();

		FName TargetBodyBone = ApplyBodyMap(SourceBodyBone);
		UpdateSourceBoneMap(SourceBodyBone);
		UpdateTargetBoneMap(TargetBodyBone);
		UpdateBoneMapTfm(SourceBodyBone, TargetBodyBone);
	}

	// Cache bone-bone effectors for creating weighted goals
	for (int PairIdx = 0; PairIdx < CachedNotifyInfo->bBodyPairsIsParentDominates.Num(); ++PairIdx)
	{
		bool bPrimaryFixed = CachedNotifyInfo->bBodyPairsIsParentDominates[PairIdx];
		
		FName PrimaryBone = CachedNotifyInfo->BodyPairs[2*PairIdx];
		FName OtherBone = CachedNotifyInfo->BodyPairs[2*PairIdx + 1];

		int32 PrimaryIdx = CheckSourceBonesMap[PrimaryBone];
		int32 OtherIdx = CheckSourceBonesMap[OtherBone];

		// Secondary (contact) Bone always affected
		CacheSourceBodyEffectVertIdx[OtherIdx].Add(2*PairIdx+1);
		if (!bPrimaryFixed)
		{
			// Primary bone can be affect if e.g. hands clapping
			CacheSourceBodyEffectVertIdx[PrimaryIdx].Add(2*PairIdx);
		}
	}

	for (int EffectIdx = 0; EffectIdx < CacheSourceEffectBones.Num(); ++EffectIdx)
	{
		if (CacheSourceBodyEffectVertIdx[EffectIdx].IsEmpty())
		{
			CacheSourceDomainBones.Add(CacheSourceEffectBones[EffectIdx]);
		}
	}
}


void FIKRetargetRelativeIKOp::OnAddedToStack(const UIKRetargeter* InRetargetAsset, const FIKRetargetOpBase* InParentOp)
{
}

FIKRetargetOpSettingsBase* FIKRetargetRelativeIKOp::GetSettings()
{
	return &Settings;
}

const UScriptStruct* FIKRetargetRelativeIKOp::GetSettingsType() const
{
	return FIKRetargetRelativeIKOpSettings::StaticStruct();
}

const UScriptStruct* FIKRetargetRelativeIKOp::GetType() const
{
	return FIKRetargetRelativeIKOp::StaticStruct();
}

const UScriptStruct* FIKRetargetRelativeIKOp::GetParentOpType() const
{
	return FIKRetargetRunIKRigOp::StaticStruct();
}

void FIKRetargetRelativeIKOp::OnParentReinitPropertyEdited(const FIKRetargetOpBase& InParentOp, const FPropertyChangedEvent* InPropertyChangedEvent)
{
}

#if WITH_EDITOR

FCriticalSection FIKRetargetRelativeIKOp::DebugDataMutex;

void FIKRetargetRelativeIKOp::DebugDraw(
	FPrimitiveDrawInterface* InPDI,
	const FTransform& InSourceTransform,
	const FTransform& InComponentTransform,
	const double InComponentScale,
	const FIKRetargetDebugDrawState& InEditorState) const
{
	if (!Settings.bDebugDraw)
	{
		return;
	}
	
	FScopeLock ScopeLock(&DebugDataMutex);

	const double SourceScale = InSourceTransform.GetScale3D().GetMax();
	FTransform SourceNoScaleTfm(InSourceTransform.GetRotation(), InSourceTransform.GetTranslation());
	const FTransform& SourceScaleCheckedTfm = (Settings.bIgnoreSourceScale) ? InSourceTransform : SourceNoScaleTfm;

	if (Settings.bDebugDrawBodyPairs)
	{
		// Draw Source body pairs
		DebugDrawBodyPairs(InPDI, SourceScaleCheckedTfm, SourceScale, DebugDrawInfo.SourcePairVerts);

		// Draw target body pairs (Possibly with "full" retarget space display)
		DebugDrawBodyPairs(InPDI, InComponentTransform, InComponentScale, DebugDrawInfo.TargetPairVerts);
	}

	if (Settings.bDebugDrawPhysicsBodies)
	{
		if (Settings.SourcePhysicsAssetOverride)
		{
			DebugDrawBodies(InPDI, SourceNoScaleTfm, SourceScale, Settings.SourcePhysicsAssetOverride, DebugDrawInfo.SourceBodyInfo);
		}
		if (Settings.TargetPhysicsAssetOverride)
		{
			DebugDrawBodies(InPDI, InComponentTransform, InComponentScale, Settings.TargetPhysicsAssetOverride, DebugDrawInfo.TargetBodyInfo);
		}
	}

	if (Settings.bDebugDrawBodyTransforms)
	{
		if (Settings.SourcePhysicsAssetOverride)
		{
			DebugDrawBodyCoords(InPDI, SourceNoScaleTfm, SourceScale, Settings.SourcePhysicsAssetOverride, DebugDrawInfo.SourceTfmInfo);
		}
		if (Settings.TargetPhysicsAssetOverride)
		{
			DebugDrawBodyCoords(InPDI, InComponentTransform, InComponentScale, Settings.TargetPhysicsAssetOverride, DebugDrawInfo.TargetTfmInfo);
		}
	}

	if (Settings.bDebugDrawRetargetVertAverages)
	{
		// Show retarget contribution average with offsets
		DebugDrawPairVertRetarget(InPDI, InComponentTransform, InComponentScale, DebugDrawInfo.PairRetargetInfo);
	}

	if (Settings.bDebugDrawGoalContributions)
	{
		// Show IK Goals (and contribs)
		DebugDrawGoalContributions(InPDI, InComponentTransform, InComponentScale, DebugDrawInfo.TargetGoals);
	}
}

void FIKRetargetRelativeIKOp::DebugDrawBodyPairs(FPrimitiveDrawInterface* InPDI, const FTransform& BaseTransform, double Scale, const TArray<FDebugDrawBodyPair>& BodyPairs) const
{
	const float MaxScale = 1.0f;
	const float MarkerSize = FMath::Min(MaxScale, static_cast<float>(Scale));
	for (const FDebugDrawBodyPair& Pair : BodyPairs)
	{
		FTransform TfmBodyA_A = FTransform(Pair.PosA_A) * BaseTransform;
		FTransform TfmBodyB_B = FTransform(Pair.PosB_B) * BaseTransform;

		IKRigDebugRendering::DrawWireCube(InPDI, TfmBodyA_A, FLinearColor::Blue,MarkerSize,MarkerSize);
		IKRigDebugRendering::DrawWireCube(InPDI, TfmBodyB_B, FLinearColor::Red,MarkerSize,MarkerSize);

		// Possibly draw full retarget debug info
		if (Pair.bFullPos)
		{
			FTransform TfmBodyA_B = FTransform(Pair.PosA_B) * BaseTransform;
			FTransform TfmBodyB_A = FTransform(Pair.PosB_A) * BaseTransform;

			IKRigDebugRendering::DrawWireCube(InPDI, TfmBodyA_B, FLinearColor::Black,MarkerSize,MarkerSize);
			IKRigDebugRendering::DrawWireCube(InPDI, TfmBodyB_A, FLinearColor::Yellow,MarkerSize,MarkerSize);

			// Draw direct relationships between bodies in retargeted A/B spaces
			DrawDashedLine(InPDI, TfmBodyA_A.GetLocation(), TfmBodyB_A.GetLocation(), FLinearColor::Black, 1, SDPG_Foreground);
			DrawDashedLine(InPDI, TfmBodyA_B.GetLocation(), TfmBodyB_B.GetLocation(), FLinearColor::Black, 1, SDPG_Foreground);

			// Yellow line showing difference between body A in A-retarget/B-retarget
			DrawDashedLine(InPDI, TfmBodyA_A.GetLocation(), TfmBodyA_B.GetLocation(), FLinearColor::Yellow, 1, SDPG_Foreground);
			// Red line showing difference between body B in A-retarget/B-retarget
			DrawDashedLine(InPDI, TfmBodyB_A.GetLocation(), TfmBodyB_B.GetLocation(), FLinearColor::Red, 1, SDPG_Foreground);
		}
		else
		{
			DrawDashedLine(InPDI, TfmBodyA_A.GetLocation(), TfmBodyB_B.GetLocation(), FLinearColor::Black, 1, SDPG_Foreground);
		}
	}
}

void FIKRetargetRelativeIKOp::DebugDrawGoalContributions(FPrimitiveDrawInterface* InPDI, const FTransform& BaseTransform, double Scale, const TArray<FDebugDrawTargetGoal>& GoalInfoLis) const
{
	// Show IK Goals (and contribs)
	const float MaxScale = 1.0f;
	const float MarkerSize = FMath::Min(MaxScale, static_cast<float>(Scale));
	for (const FDebugDrawTargetGoal& Goal : GoalInfoLis)
	{
		FTransform GoalTfm = FTransform(Goal.Goal) * BaseTransform;
		FVector GoalPoint = GoalTfm.GetTranslation();
		for (const FVector& v: Goal.PairTargets)
		{
			FTransform ContribTfm = FTransform(v) * BaseTransform;
			FVector ContribPt = ContribTfm.GetTranslation();
			DrawDashedLine(InPDI, ContribPt, GoalPoint, FLinearColor::Red, 1, SDPG_Foreground);
			IKRigDebugRendering::DrawWireCube(InPDI, ContribTfm, FLinearColor::White,MarkerSize,MarkerSize);
		}
		IKRigDebugRendering::DrawWireCube(InPDI, GoalTfm, FLinearColor::Yellow,MarkerSize,MarkerSize);
	}
}

void FIKRetargetRelativeIKOp::DebugDrawPairVertRetarget(FPrimitiveDrawInterface* InPDI, const FTransform& BaseTransform, double Scale, const TArray<FDebugRelativeTargetPairSpace>& RetargetPairList) const
{
	const float MaxScale = 1.0f;
	const float MarkerSize = 0.5f * FMath::Min(MaxScale, static_cast<float>(Scale));
	for (const FDebugRelativeTargetPairSpace& Info : RetargetPairList)
	{
		FTransform V1Tfm = FTransform(Info.PairRangeStart) * BaseTransform;
		FTransform V2Tfm = FTransform(Info.PairRangeEnd) * BaseTransform;
		IKRigDebugRendering::DrawWireCube(InPDI, V1Tfm, FLinearColor::Red,MarkerSize,MarkerSize);
		IKRigDebugRendering::DrawWireCube(InPDI, V2Tfm, FLinearColor::Red,MarkerSize,MarkerSize);

		FVector V1 = V1Tfm.GetTranslation();
		FVector V2 = V2Tfm.GetTranslation();
		// DrawDashedLine(InPDI, VBone, V1, FLinearColor::Yellow, 1, SDPG_Foreground);
		// DrawDashedLine(InPDI, VBone, V2, FLinearColor::Yellow, 1, SDPG_Foreground);
		DrawDashedLine(InPDI, V1, V2, FLinearColor::Black, 1, SDPG_Foreground);

		FTransform VAvgTfm = FTransform(Info.PairTarget) * BaseTransform;
		IKRigDebugRendering::DrawWireCube(InPDI, VAvgTfm, FLinearColor::Gray,MarkerSize,MarkerSize);

		FVector FeasibleMinV =  FMath::Lerp(Info.PairRangeStart, Info.PairRangeEnd, Info.FeasibleRange.Min);
		FTransform VMinTfm = FTransform(FeasibleMinV) * BaseTransform;
		IKRigDebugRendering::DrawWireCube(InPDI, VMinTfm, FLinearColor::Yellow,MarkerSize,MarkerSize);

		FVector FeasibleMaxV =  FMath::Lerp(Info.PairRangeStart, Info.PairRangeEnd, Info.FeasibleRange.Max);
		FTransform VMaxTfm = FTransform(FeasibleMaxV) * BaseTransform;
		IKRigDebugRendering::DrawWireCube(InPDI, VMaxTfm, FLinearColor::Green,MarkerSize,MarkerSize);
		// DrawDashedLine(InPDI, VOffset, VOffset + t.Get<3>(), FLinearColor::Black, 1, SDPG_Foreground);
	}
}

void FIKRetargetRelativeIKOp::DebugDrawBodies(FPrimitiveDrawInterface* InPDI, const FTransform& BaseTransform, double Scale, UPhysicsAsset* PhysAsset, const TArray<FDebugDrawBodyInfo>& PhysBodies) const
{
	for (const FDebugDrawBodyInfo& BodyInfo : PhysBodies)
	{
		const FKShapeElem* ShapeElem = FindBodyShape(PhysAsset, BodyInfo.BodyName);
		if (!ShapeElem)
		{
			continue;
		}
		
		FTransform CompTfm = BodyInfo.BoneTfm * BaseTransform;

		// Draw physics body
		DebugDrawPhysBody(InPDI, CompTfm, Scale, ShapeElem, FLinearColor::Yellow);
	}
}

void FIKRetargetRelativeIKOp::DebugDrawPhysBody(FPrimitiveDrawInterface* InPDI, const FTransform& ParentTransform, double Scale, const FKShapeElem* ShapeElem, const FLinearColor& Color) const
{
	FTransform BodyFrame = ShapeElem->GetTransform() * ParentTransform;

	const FVector Translation = BodyFrame.GetLocation();
	const FVector UnitXAxis = BodyFrame.GetUnitAxis( EAxis::X );
	const FVector UnitYAxis = BodyFrame.GetUnitAxis( EAxis::Y );
	const FVector UnitZAxis = BodyFrame.GetUnitAxis( EAxis::Z );
	
	switch (ShapeElem->GetShapeType())
	{
	case EAggCollisionShape::Box:
		{
			const FKBoxElem* BoxElem = static_cast<const FKBoxElem*>(ShapeElem);
			const FVector Extent = 0.5 * FVector(BoxElem->X, BoxElem->Y, BoxElem->Z) * Scale;
			DrawOrientedWireBox(InPDI, Translation, UnitXAxis, UnitYAxis, UnitZAxis, Extent, Color, SDPG_Foreground);
			return;
		}
	case EAggCollisionShape::Sphyl:
		{
			const FKSphylElem* CapsuleElem = static_cast<const FKSphylElem*>(ShapeElem);
			const double Radius = CapsuleElem->Radius * Scale;
			const double HalfHeight = (0.5*CapsuleElem->Length + CapsuleElem->Radius) * Scale;
			DrawWireCapsule(InPDI, Translation, UnitXAxis, UnitYAxis, UnitZAxis, Color,
						Radius, HalfHeight,25, SDPG_Foreground, 0, 1.0);
			return;
		}
	case EAggCollisionShape::Sphere:
		{
			const FKSphereElem* SphereElem = static_cast<const FKSphereElem*>(ShapeElem);
			const double Radius = SphereElem->Radius * Scale;
			DrawWireSphere(InPDI, Translation, Color, Radius, 25, SDPG_Foreground);
			return;
		}
	default:
		return;
	}
}


void FIKRetargetRelativeIKOp::DebugDrawBodyCoords(FPrimitiveDrawInterface* InPDI, const FTransform& BaseTransform, double Scale, UPhysicsAsset* PhysAsset, const TArray<FDebugBodyTfmInfo>& BodyTfms) const
{
	// Stop scale from making coords too large
	const float MaxScale = 1.0f;
	const float CoordSize = 10.0f * FMath::Min(MaxScale, static_cast<float>(Scale));
	const float CoordThickness = 0.5f * FMath::Min(MaxScale, static_cast<float>(Scale));

	for (const FDebugBodyTfmInfo& BodyTfm : BodyTfms)
	{

		FVector StartV = BaseTransform.TransformPosition(BodyTfm.Center);
		FVector CoordX = BaseTransform.TransformPosition(BodyTfm.TfmX);
		FVector CoordY = BaseTransform.TransformPosition(BodyTfm.TfmY);
		FVector CoordZ = BaseTransform.TransformPosition(BodyTfm.TfmZ);
		
		InPDI->DrawLine(StartV, CoordX, FLinearColor::Red, SDPG_Foreground, CoordThickness);
		InPDI->DrawLine(StartV, CoordY, FLinearColor::Green, SDPG_Foreground, CoordThickness);
		InPDI->DrawLine(StartV, CoordZ, FLinearColor::Blue, SDPG_Foreground, CoordThickness);
	}
}



void FIKRetargetRelativeIKOp::ResetDebugInfo()
{
	FScopeLock ScopeLock(&DebugDataMutex);
	
	DebugDrawInfo.SourcePairVerts.Reset();
	DebugDrawInfo.TargetPairVerts.Reset();
	
	DebugDrawInfo.SourceBodyInfo.Reset();
	DebugDrawInfo.TargetBodyInfo.Reset();

	DebugDrawInfo.SourceBodyInfo.Reset();
	DebugDrawInfo.TargetTfmInfo.Reset();
	
	DebugDrawInfo.PairRetargetInfo.Reset();
	DebugDrawInfo.TargetGoals.Reset();
}

#endif //WITH_EDITOR

FIKRetargetRelativeIKOpSettings UIKRetargetRelativeIKController::GetSettings()
{
	return *reinterpret_cast<FIKRetargetRelativeIKOpSettings*>(OpSettingsToControl);
}

void UIKRetargetRelativeIKController::SetSettings(FIKRetargetRelativeIKOpSettings InSettings)
{
	OpSettingsToControl->CopySettingsAtRuntime(&InSettings);
}

#undef LOCTEXT_NAMESPACE
// Copyright Epic Games, Inc. All Rights Reserved.

#include "BodyIntersectIKOp.h"

#include "IKRigObjectVersion.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/SkeletalBodySetup.h"
#include "Retargeter/IKRetargetProcessor.h"
#include "Retargeter/RetargetOps/RunIKRigOp.h"
#include "Rig/Solvers/PointsToRotation.h"

#if WITH_EDITOR
#include "PrimitiveDrawingUtils.h"
#include "Engine/EngineTypes.h"
#endif

#define LOCTEXT_NAMESPACE "BodyIntersectIKOp"


const UClass* FIKRetargetBodyIntersectIKOpSettings::GetControllerType() const
{
	return UIKRetargetBodyIntersectController::StaticClass();
}

void FIKRetargetBodyIntersectIKOpSettings::CopySettingsAtRuntime(const FIKRetargetOpSettingsBase* InSettingsToCopyFrom)
{
	static TArray<FName> PropertiesToIgnore = {"PhysicsAssetOverride", "IntersectGoals", "IntersectBodies"};
	FIKRetargetOpBase::CopyStructProperties(
		FIKRetargetBodyIntersectIKOpSettings::StaticStruct(),
		InSettingsToCopyFrom,
		this,
		PropertiesToIgnore);
}

bool FIKRetargetBodyIntersectIKOp::Initialize(
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
		LOCTEXT("MissingIKRig", "{0}, is missing an IK rig. No chains can be retargeted. "), FText::FromName(GetName())));
		return false;
	}

	PhysicsAsset = Settings.TargetPhysicsAssetOverride;

	if (!PhysicsAsset)
	{
		InLog.LogWarning( FText::Format(
		LOCTEXT("MissingPhysicsAsset", "{0}: Intersect Physics Asset must be specified. "), FText::FromName(GetName())));
		return false;
	}

	UpdateCacheSkelInfo(InTargetSkeleton);
	
	const FIKRigGoalContainer& GoalContainer = InProcessor.GetIKRigGoalContainer();
	for (const FName& GoalName : Settings.IntersectGoals)
	{
		const FIKRigGoal* IKGoal = GoalContainer.FindGoalByName(GoalName);
		if (!IKGoal)
		{
			continue;
		}

		// TODO: Assumes goal bone has a phys body associated with it
		UpdateTargetBoneMap(IKGoal->BoneName);
	}
	
	bIsInitialized = true;
	return bIsInitialized;
}

void FIKRetargetBodyIntersectIKOp::Run(
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

	if (!bIsInitialized)
	{
		return;
	}

#if WITH_EDITOR
	FDebugBodyIntersectDrawInfo LocalDebugInfo;
#endif //WITH_EDITOR

	FIKRigGoalContainer& GoalContainer = InProcessor.GetIKRigGoalContainer();
	for (const FName& GoalName : Settings.IntersectGoals)
	{
		FIKRigGoal* IKGoal = GoalContainer.FindGoalByName(GoalName);
		if (!IKGoal || !IKGoal->bEnabled)
		{
			continue;
		}

		const FName TargetBoneName = IKGoal->BoneName;
		if (!CacheTargetSkelIndices.Contains(TargetBoneName))
		{
			// UE_LOG(LogAnimation, Warning, TEXT("---- Could not find cache bone: %s"), *TargetBoneName.ToString());
			continue;
		}
		
		FKShapeElem* GoalShape = FindBodyShape(PhysicsAsset, IKGoal->BoneName);
		if (!GoalShape)
		{
			continue;
		}

		const int32 TargetBoneIdx = CacheTargetSkelIndices[TargetBoneName];
		const FTransform TargetBoneTfm = OutTargetGlobalPose[TargetBoneIdx];
		
		double GoalRadius = CalcShapeSmallRadius(GoalShape);
		if (FMath::IsNearlyZero(GoalRadius))
		{
			continue;
		}

		// TODO: Handle actual capsule offsets w/ a lever arm for rotation?
		FVector CompGoalUpdate = GoalLocBlendCompSpace(IKGoal, TargetBoneTfm);
		for (FName BodyName : Settings.IntersectBodies)
		{
			if (TargetBoneName == BodyName)
			{
				continue;
			}
				
			if (!CacheTargetSkelIndices.Contains(BodyName))
			{
				continue;
			}
				
			int32 BodyPoseIdx = CacheTargetSkelIndices[BodyName];
			FTransform BodyTfm = OutTargetGlobalPose[BodyPoseIdx];
			FKShapeElem* ShapeElem = FindBodyShape(PhysicsAsset, BodyName);
			if (!ShapeElem)
			{
				continue;
			}

			FVector DeltaDir = FVector::ZeroVector;
			double Dist = CalcIntersectionDelta(CompGoalUpdate, GoalRadius, BodyTfm, ShapeElem, DeltaDir);
			if (Dist > 0.0)
			{
				CompGoalUpdate += Dist * DeltaDir;
			}
		}
		SetGoalPosFromCompSpace(IKGoal, TargetBoneTfm, CompGoalUpdate);
#if WITH_EDITOR
		if (Settings.bDebugDraw)
		{
			LocalDebugInfo.TestSpheres.Emplace(CompGoalUpdate, GoalRadius);
		}
#endif //WITH_EDITOR
	}

#if WITH_EDITOR
	if (Settings.bDebugDraw)
	{
		FScopeLock ScopeLock(&DebugDataMutex);
		
		DebugDrawInfo.TestSpheres = LocalDebugInfo.TestSpheres;

		// Cache debug info for target domain bodies
		DebugDrawInfo.TargetIntersectTfms.Reset(Settings.IntersectBodies.Num());
		for (FName TargetBoneName : Settings.IntersectBodies)
		{
			// TODO: Only show domain bodies? 
			if (!CacheTargetSkelIndices.Contains(TargetBoneName))
			{
				continue;
			}
			
			int32 TargetPoseIdx = CacheTargetSkelIndices[TargetBoneName];
			DebugDrawInfo.TargetIntersectTfms.Add({TargetBoneName,OutTargetGlobalPose[TargetPoseIdx]});
		}
	}
#endif
}

FVector FIKRetargetBodyIntersectIKOp::GoalLocBlendCompSpace(const FIKRigGoal* Goal, const FTransform& BoneTfm) const
{
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
		{
			// We assume no World-space goals will be set using retarget stack
		}
	}

	return BoneTfm.GetLocation();
}

void FIKRetargetBodyIntersectIKOp::SetGoalPosFromCompSpace(FIKRigGoal* Goal, const FTransform& BoneTfm, const FVector& CompSpaceLoc) const
{
	// TODO: Alpha passthrough and/or alpha ignore
	// Should we just update position space to be component always?
	switch (Goal->PositionSpace)
	{
	case EIKRigGoalSpace::Additive:
		{
			Goal->Position = CompSpaceLoc - BoneTfm.GetLocation();
			Goal->PositionAlpha = 1.0f;
			break;
		}
	case EIKRigGoalSpace::Component:
		{
			Goal->Position = CompSpaceLoc;
			Goal->PositionAlpha = 1.0f;
			break;
		}
	case EIKRigGoalSpace::World:
	default:
		{
			// We assume no World-space goals will be set using retarget stack
			Goal->Position = CompSpaceLoc;
			Goal->PositionAlpha = 1.0f;
			break;
		}
	}
}

double FIKRetargetBodyIntersectIKOp::CalcIntersectionDelta(const FVector& TargetPoint, double TargetRadius, const FTransform& ShapeTfm, FKShapeElem* ShapeElem, FVector& OutDeltaDir)
{
	if (!ShapeElem)
	{
		return -1.0;
	}

	float Dist = 0.0f;
	FVector ClosestPoint;
	switch (ShapeElem->GetShapeType())
	{
	case EAggCollisionShape::Sphere:
		{
			FKSphereElem* SphereElem = static_cast<FKSphereElem*>(ShapeElem);
			Dist = SphereElem->GetClosestPointAndNormal(TargetPoint, ShapeTfm, ClosestPoint, OutDeltaDir);
			break;
		}

	case EAggCollisionShape::Sphyl:
		{
			FKSphylElem* CapsuleElem = static_cast<FKSphylElem*>(ShapeElem);
			Dist = CapsuleElem->GetClosestPointAndNormal(TargetPoint, ShapeTfm, ClosestPoint, OutDeltaDir);
			break;
		}

	// case EAggCollisionShape::Box:
	// 	{
	// 		const FKBoxElem* BoxElem = static_cast<const FKBoxElem*>(ShapeElem);
	// 		Dist = BoxElem->GetClosestPointAndNormal(TargetPoint, ShapeTfm, ClosestPoint, OutDeltaDir);
	// 		break;
	// 	}

	default:
		{
			return -1.0;
		}
	}

	return TargetRadius - Dist;
}

double FIKRetargetBodyIntersectIKOp::CalcShapeSmallRadius(FKShapeElem* ShapeElem)
{
	switch (ShapeElem->GetShapeType())
	{
	case EAggCollisionShape::Sphere:
		{
			FKSphereElem* SphereElem = static_cast<FKSphereElem*>(ShapeElem);
			return SphereElem->Radius;
		}

	case EAggCollisionShape::Sphyl:
		{
			FKSphylElem* CapsuleElem = static_cast<FKSphylElem*>(ShapeElem);
			return CapsuleElem->Radius;
		}

	default:
		{
			return 0.0;
		}
	}
}

double FIKRetargetBodyIntersectIKOp::CalcShapeAvgRadius(FKShapeElem* ShapeElem)
{
	switch (ShapeElem->GetShapeType())
	{
	case EAggCollisionShape::Sphere:
		{
			FKSphereElem* SphereElem = static_cast<FKSphereElem*>(ShapeElem);
			return SphereElem->Radius;
		}

	case EAggCollisionShape::Sphyl:
		{
			FKSphylElem* CapsuleElem = static_cast<FKSphylElem*>(ShapeElem);
			return CapsuleElem->Radius + 0.25*CapsuleElem->Length;
		}

	default:
		{
			return 0.0;
		}
	}
}

FKShapeElem* FIKRetargetBodyIntersectIKOp::FindBodyShape(const UPhysicsAsset* PhysAsset, FName BoneName)
{
	int32 BodyIdx = PhysAsset->FindBodyIndex(BoneName);
	if (BodyIdx == INDEX_NONE)
	{
		UE_LOG(LogAnimation, Warning, TEXT("No body index found: %s"), *BoneName.ToString());
		return nullptr;
	}
	return PhysAsset->SkeletalBodySetups[BodyIdx]->AggGeom.GetElement(0);
}

void FIKRetargetBodyIntersectIKOp::UpdateTargetBoneMap(FName TargetBoneName)
{
	int32 TargetIdx = TargetBoneNames.Find(TargetBoneName);

	if (TargetIdx == INDEX_NONE)
	{
		return;
	}
	
	CacheTargetSkelIndices.Emplace(TargetBoneName, TargetIdx);
}

void FIKRetargetBodyIntersectIKOp::UpdateCacheSkelInfo(const FTargetSkeleton& InTargetSkeleton)
{
	TargetBoneNames = InTargetSkeleton.BoneNames;
	CacheTargetSkelIndices.Reset();
	
	for (FName TargetBodyName : Settings.IntersectBodies)
	{
		UpdateTargetBoneMap(TargetBodyName);
	}
}

void FIKRetargetBodyIntersectIKOp::OnAddedToStack(const UIKRetargeter* InRetargetAsset, const FIKRetargetOpBase* InParentOp)
{
}

FIKRetargetOpSettingsBase* FIKRetargetBodyIntersectIKOp::GetSettings()
{
	return &Settings;
}

const UScriptStruct* FIKRetargetBodyIntersectIKOp::GetSettingsType() const
{
	return FIKRetargetBodyIntersectIKOpSettings::StaticStruct();
}

const UScriptStruct* FIKRetargetBodyIntersectIKOp::GetType() const
{
	return FIKRetargetBodyIntersectIKOp::StaticStruct();
}

const UScriptStruct* FIKRetargetBodyIntersectIKOp::GetParentOpType() const
{
	return FIKRetargetRunIKRigOp::StaticStruct();
}

#if WITH_EDITOR

FCriticalSection FIKRetargetBodyIntersectIKOp::DebugDataMutex;

void FIKRetargetBodyIntersectIKOp::DebugDraw(
	FPrimitiveDrawInterface* InPDI,
	const FTransform& InSourceTransform,
	const FTransform& InComponentTransform,
	const double InComponentScale,
	const FIKRetargetDebugDrawState& InEditorState) const
{
	FScopeLock ScopeLock(&DebugDataMutex);

	if (!Settings.TargetPhysicsAssetOverride)
	{
		return;
	}

	for (const auto& IntersectBody : DebugDrawInfo.TargetIntersectTfms)
	{
		const FName BodyName = IntersectBody.Get<0>();
		const FKShapeElem* ShapeElem = FindBodyShape(Settings.TargetPhysicsAssetOverride, BodyName);
		if (!ShapeElem)
		{
			continue;
		}
		
		FTransform CompTfm = IntersectBody.Get<1>() * InComponentTransform;
		DebugDrawPhysBody(InPDI, CompTfm, InComponentScale, ShapeElem, FLinearColor::Yellow);
	}

	for (const auto& Sphere : DebugDrawInfo.TestSpheres)
	{
		FVector Center = InComponentTransform.TransformPosition(Sphere.Get<0>());
		DrawWireSphere(InPDI, Center, FLinearColor::Red, Sphere.Get<1>()*InComponentScale, 25, SDPG_Foreground);
	}
}

void FIKRetargetBodyIntersectIKOp::DebugDrawPhysBody(FPrimitiveDrawInterface* InPDI, const FTransform& ParentTransform, double Scale, const FKShapeElem* ShapeElem, const FLinearColor& Color) const
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



void FIKRetargetBodyIntersectIKOp::ResetDebugInfo()
{
	FScopeLock ScopeLock(&DebugDataMutex);
	
	DebugDrawInfo.TargetIntersectTfms.Reset();
	DebugDrawInfo.TestSpheres.Reset();
}

#endif //WITH_EDITOR

FIKRetargetBodyIntersectIKOpSettings UIKRetargetBodyIntersectController::GetSettings()
{
	return *reinterpret_cast<FIKRetargetBodyIntersectIKOpSettings*>(OpSettingsToControl);
}

void UIKRetargetBodyIntersectController::SetSettings(FIKRetargetBodyIntersectIKOpSettings InSettings)
{
	OpSettingsToControl->CopySettingsAtRuntime(&InSettings);
}

#undef LOCTEXT_NAMESPACE
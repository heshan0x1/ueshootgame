// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Retargeter/IKRetargetOps.h"
#include "Retargeter/IKRetargetSettings.h"

#include "BodyIntersectIKOp.generated.h"

#define UE_API BODYINTERSECTIKOP_API

#define LOCTEXT_NAMESPACE "BodyIntersectIKOp"

struct FIKRigGoal;
struct FKShapeElem;
class UPhysicsAsset;

// Used to hold all debug draw info for convenience
struct FDebugBodyIntersectDrawInfo
{
	// Intersect info
	TArray<TTuple<FName,FTransform>> TargetIntersectTfms;
	TArray<TTuple<FVector,double>> TestSpheres;
};

USTRUCT(BlueprintType, meta = (DisplayName = "Body Intersect IK Settings"))
struct FIKRetargetBodyIntersectIKOpSettings : public FIKRetargetOpSettingsBase
{
	GENERATED_BODY()

	UE_API virtual const UClass* GetControllerType() const override;

	UE_API virtual void CopySettingsAtRuntime(const FIKRetargetOpSettingsBase* InSettingsToCopyFrom) override;

	// Target physics asset for checking intersections against
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics", meta=(ReinitializeOnEdit))
	TObjectPtr<UPhysicsAsset> TargetPhysicsAssetOverride;

	// IK Goals to check sphere intersections (Goal effector bone should have a sphere or capsule body!)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intersection", meta=(ReinitializeOnEdit))
	TArray<FName> IntersectGoals;

	// Physics bodies to do trivial intersection against
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intersection", meta=(ReinitializeOnEdit))
	TArray<FName> IntersectBodies;
};

USTRUCT(BlueprintType, meta = (DisplayName = "Body Intersect Goals"))
struct FIKRetargetBodyIntersectIKOp : public FIKRetargetOpBase
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

	UE_API virtual FIKRetargetOpSettingsBase* GetSettings() override;
	
	UE_API virtual const UScriptStruct* GetSettingsType() const override;
	
	UE_API virtual const UScriptStruct* GetType() const override;
	
	UE_API virtual const UScriptStruct* GetParentOpType() const override;

	UPROPERTY()
	FIKRetargetBodyIntersectIKOpSettings Settings;

private:
	void UpdateCacheSkelInfo(const FTargetSkeleton& InTargetSkeleton);
	void UpdateTargetBoneMap(FName TargetBoneName);

	FVector GoalLocBlendCompSpace(const FIKRigGoal* Goal, const FTransform& BoneTfm) const;
	void SetGoalPosFromCompSpace(FIKRigGoal* Goal, const FTransform& BoneTfm, const FVector& CompSpaceLoc) const;
	
	static double CalcIntersectionDelta(const FVector&  TargetPoint, double TargetRadius, const FTransform& ShapeTfm, FKShapeElem* ShapeElem, FVector& OutDeltaDir);
	
	static double CalcShapeSmallRadius(FKShapeElem* ShapeElem);
	static double CalcShapeAvgRadius(FKShapeElem* ShapeElem);

	static FKShapeElem* FindBodyShape(const UPhysicsAsset* PhysAsset, FName BoneName);

#if WITH_EDITOR
public:
	UE_API virtual void DebugDraw(
		FPrimitiveDrawInterface* InPDI,
		const FTransform& InSourceTransform,
		const FTransform& InComponentTransform,
		const double InComponentScale,
		const FIKRetargetDebugDrawState& InEditorState) const override;

	UE_API virtual bool HasDebugDrawing() override { return true; };

private:
	void DebugDrawPhysBody(FPrimitiveDrawInterface* InPDI, const FTransform& ParentTransform, double Scale, const FKShapeElem* ShapeElem, const FLinearColor& Color) const;
	
	void ResetDebugInfo();

	// Collection of all debug structures
	FDebugBodyIntersectDrawInfo DebugDrawInfo;
	
	static UE_API FCriticalSection DebugDataMutex;
#endif
private:
	TObjectPtr<const UPhysicsAsset> PhysicsAsset;

	// Cache ik goals to check
	TArray<const FIKRigGoal*> IntersectIKGoals;
	
	// Cache source/target bone-names
	TArray<FName> TargetBoneNames;

	// Cache target skel index info
	TMap<FName,int32> CacheTargetSkelIndices;
};

/* The blueprint/python API for editing BodyIntersectIK Op */
UCLASS(MinimalAPI, BlueprintType)
class UIKRetargetBodyIntersectController : public UIKRetargetOpControllerBase
{
	GENERATED_BODY()
	
public:

	/* Get the current op settings as a struct.
	 * @return FIKRetargetBodyIntersectIKOp struct with the current settings used by the op. */
	UFUNCTION(BlueprintCallable, Category = Settings)
	UE_API FIKRetargetBodyIntersectIKOpSettings GetSettings();

	/* Set the solver settings. Input is a custom struct type for this solver.
	 * @param InSettings a FIKRetargetBodyIntersectIKOp struct containing all the settings to apply to this op */
	UFUNCTION(BlueprintCallable, Category = Settings)
	UE_API void SetSettings(FIKRetargetBodyIntersectIKOpSettings InSettings);
};

#undef LOCTEXT_NAMESPACE

#undef UE_API

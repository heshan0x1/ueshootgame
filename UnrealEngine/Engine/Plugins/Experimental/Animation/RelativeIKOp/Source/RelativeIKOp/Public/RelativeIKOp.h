// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Retargeter/IKRetargetOps.h"
#include "Retargeter/IKRetargetSettings.h"
#include "RelativeIKOp.generated.h"

#define UE_API RELATIVEIKOP_API

#define LOCTEXT_NAMESPACE "RelativeIKOp"

struct FIKRigGoal;
struct FKShapeElem;
struct FResolvedBoneChain;
struct FAnimMontageInstance;
struct FIKRetargetRunIKRigOp;
class UAnimInstance;
class UAnimSequence;
class UPhysicsAsset;
class URelativeBodyBakeAnimNotify;

// Debug info for individual body pair contributions
struct FDebugRelativeTargetPairSpace
{
	FVector PairTarget;
	
	FVector PairRangeStart;
	FVector PairRangeEnd;
	
	FDoubleInterval FeasibleRange;
};

// Debug draw info for goal (with individual pair goal targets)
struct FDebugDrawTargetGoal
{
	FVector Goal;
	TArray<FVector> PairTargets;
};

// Debug draw info for source or target pair
struct FDebugDrawBodyPair
{
	FVector PosA_A;
	FVector PosB_B;
	
	bool bFullPos;
	FVector PosB_A;
	FVector PosA_B;
};

// Debug physics body info
struct FDebugDrawBodyInfo
{
	FName BodyName;
	FTransform BoneTfm;
	FTransform RetargetTfm = FTransform::Identity;
};

struct FDebugBodyTfmInfo
{
	FVector Center;
	FVector TfmX;
	FVector TfmY;
	FVector TfmZ;
};

// Used to hold all debug draw info for convenience
struct FDebugRelativeIKDrawInfo
{
	// Source/target vert pair info
	TArray<FDebugDrawBodyPair> SourcePairVerts;
	TArray<FDebugDrawBodyPair> TargetPairVerts;

	// Body info
	TArray<FDebugDrawBodyInfo> SourceBodyInfo;
	TArray<FDebugDrawBodyInfo> TargetBodyInfo;

	// Body transform vecs
	TArray<FDebugBodyTfmInfo> SourceTfmInfo;
	TArray<FDebugBodyTfmInfo> TargetTfmInfo;

	// Debug info for retarget pairs
	TArray<FDebugRelativeTargetPairSpace> PairRetargetInfo;
	// Final goal points with contributions
	TArray<FDebugDrawTargetGoal> TargetGoals;
};

USTRUCT(BlueprintType, meta = (DisplayName = "Relative IK Settings"))
struct FIKRetargetRelativeIKOpSettings : public FIKRetargetOpSettingsBase
{
	GENERATED_BODY()

	UE_API virtual const UClass* GetControllerType() const override;

	UE_API virtual void CopySettingsAtRuntime(const FIKRetargetOpSettingsBase* InSettingsToCopyFrom) override;

	// Source mesh physics asset for relative body pair tests 
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics", meta=(ReinitializeOnEdit))
	TObjectPtr<UPhysicsAsset> SourcePhysicsAssetOverride;

	// Target mesh physics asset for retargeting relative body pair vertices
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics", meta=(ReinitializeOnEdit))
	TObjectPtr<UPhysicsAsset> TargetPhysicsAssetOverride;

	// Physics body source -> target name mapping 
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Source Target Mapping", meta=(ReinitializeOnEdit))
	TMap<FName,FName> BodyMapping;
	
	// Maximum distance for which body pair info is baked
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Parameters", meta=(ClampMin = "0.1"))
	double DistanceThreshold = 50.00;

	// Maximum distance for which body pair info is baked
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Parameters", meta=(ClampMin = "0"))
	double DistanceFade = 200.00;

	// Bias feasibility distance weighting relative to distance threshold (+ increase target feasibility/- reduced target feasibility)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Parameters")
	double FeasibilityLengthBias = 0.0;

	// IK Goal normalization (internally multiplied by total of contribution weights)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Parameters", meta=(ClampMin = "0", ClampMax = "1"))
	double ContributionSumWeight = 1.0;

	// Frames of temporal smoothing of body pair verts (0 is no smoothing)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Parameters", meta=(ClampMin = "0", ClampMax = "60"))
	int32 TemporalSmoothingRadius = 15;

	// Ignore source scaling when computing relative distance relationships
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Parameters")
	bool bIgnoreSourceScale = true;

	// Alpha from contact-body to secondary body representation in contribution pairs
	UPROPERTY(BlueprintReadWrite, Category = "Parameters")
	double RetargetContactAlpha = 0.5;

	// Alpha between primary and secondary pair distance relationship
	UPROPERTY(BlueprintReadWrite, Category = "Parameters")
	double RetargetSpringAlpha = 0.5;

	// Draw source and target body pair relationships for current animation
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
	bool bDebugDrawBodyPairs = false;

	// Draw full retarget space (quad) pair relationships
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
	TArray<FName> DebugFullRetargetPairBones;

	// Draw each pair's goal contribution (white) and show weighted final goal location (yellow)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
	bool bDebugDrawGoalContributions = false;

	// Draw retarget pair-line for each pair contribution 
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
	bool bDebugDrawRetargetVertAverages = false;

	// Display source and target physics bodies for baked data
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
	bool bDebugDrawPhysicsBodies = false;

	// Show body-local transform setup (like baked verts)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
	bool bDebugDrawBodyTransforms = false;

	// Run op and display debug info but DON'T update IK Goals
	UPROPERTY(BlueprintReadWrite, Category = "Debug")
	bool bDryRun = false;

	// Use distance to pair contact to blend towards direct contact
	UPROPERTY(BlueprintReadWrite, Category = "Test Features")
	bool bTestDistContactAlpha = true;

	// Dial back distance weighting based on feasibility length of target bone chain
	UPROPERTY(BlueprintReadWrite, Category = "Test Features")
	bool bTestFeasibilityWeight = true;

	// Use target (default) when computing pair alternate representations
	UPROPERTY(BlueprintReadWrite, Category = "Test Features")
	bool bTestRetargetScale = true;
};

USTRUCT(BlueprintType, meta = (DisplayName = "Relative IK Goals"))
struct FIKRetargetRelativeIKOp : public FIKRetargetOpBase
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

	UE_API virtual void AnimGraphPreUpdateMainThread(USkeletalMeshComponent& SourceMeshComponent, USkeletalMeshComponent& TargetMeshComponent) override;

	UE_API virtual void OnAddedToStack(const UIKRetargeter* InRetargetAsset, const FIKRetargetOpBase* InParentOp) override;

	UE_API virtual FIKRetargetOpSettingsBase* GetSettings() override;
	
	UE_API virtual const UScriptStruct* GetSettingsType() const override;
	
	UE_API virtual const UScriptStruct* GetType() const override;
	
	UE_API virtual const UScriptStruct* GetParentOpType() const override;

	UE_API virtual void OnParentReinitPropertyEdited(const FIKRetargetOpBase& InParentOp, const FPropertyChangedEvent* InPropertyChangedEvent) override;

	UPROPERTY()
	FIKRetargetRelativeIKOpSettings Settings;

private:
	void UpdateCacheBoneChains(const FIKRetargetProcessor& InProcessor, const FIKRetargetRunIKRigOp* ParentRigOp);
	void UpdateSourceBoneMap(FName SourceBoneName);
	void UpdateTargetBoneMap(FName TargetBoneName);
	void UpdateBoneMapTfm(FName SourceBoneName, FName TargetBoneName);
	void UpdateCacheSkelInfo(const FRetargetSkeleton& InSourceSkeleton, const FTargetSkeleton& InTargetSkeleton);
	void PreUpdateMontagePlayhead();
	void SetupRelativeIKNotifyInfoMontage();
	void UpdateRelativeIKNotifyInfoAnimSeq(UAnimInstance* SourceAnimInstance);
	void ResetCacheNotifyInfo();
	void UpdateCacheNotifyInfo(URelativeBodyBakeAnimNotify* NotifyInfo);

	FName ApplyBodyMap(FName BodyName);
	bool HasBoneDelta(FName TargetBoneName) const;
	const FTransform& GetTargetBoneDelta(FName TargetBoneName) const;
	void UpdateCacheChainInfo(const TArray<FTransform>& TargetPose, FName BoneName);
	
	void ComputeFeasibilityRange(FDoubleInterval& OutFeasibleRange, FName BoneName, FVector BoneChainOffset, FVector TargetRelativeToBoneV, FVector TargetRelativeToOpposeBoneV);
	TConstArrayView<FVector3f> ApplyTemporalSmoothing(float Time);

	FVector GoalLocBlendCompSpace(const FIKRigGoal* Goal, const FTransform& BoneTfm) const;

	static bool CalcLineSphereIntersect(FDoubleInterval& OutRange, const FVector& Center, double Radius, const FVector& StartPoint, const FVector& EndPoint);

	static FTransform CalcReferenceShapeScale3D(FKShapeElem* ShapeElem);
	// Get local body rotation relative to parent bone
	static FTransform GetBodyRotation(const UPhysicsAsset* PhysAsset, FName BoneName);
	// Get body origin relative to parent bone
	static FTransform GetBodyTranslation(const UPhysicsAsset* PhysAsset, FName BoneName);
	// Get body oriented (non-axis aligned scaling) relative to parent bone
	static FMatrix GetBodyOrientedScale(const UPhysicsAsset* PhysAsset, FName BoneName);
	static FKShapeElem* FindBodyShape(const UPhysicsAsset* PhysAsset, FName BoneName);
	
	static double GetDistanceWeight(double Distance, double DistThreshold, double ScalarFade = 200.0);

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
	void DebugDrawBodyPairs(FPrimitiveDrawInterface* InPDI, const FTransform& BaseTransform, double Scale, const TArray<FDebugDrawBodyPair>& BodyPairs) const;
	void DebugDrawGoalContributions(FPrimitiveDrawInterface* InPDI, const FTransform& BaseTransform, double Scale, const TArray<FDebugDrawTargetGoal>& GoalInfoLis) const;
	void DebugDrawPairVertRetarget(FPrimitiveDrawInterface* InPDI, const FTransform& BaseTransform, double Scale, const TArray<FDebugRelativeTargetPairSpace>& RetargetPairList) const;
	void DebugDrawBodies(FPrimitiveDrawInterface* InPDI, const FTransform& BaseTransform, double Scale, UPhysicsAsset* PhysAsset, const TArray<FDebugDrawBodyInfo>& PhysBodies) const;
	void DebugDrawPhysBody(FPrimitiveDrawInterface* InPDI, const FTransform& ParentTransform, double Scale, const FKShapeElem* ShapeElem, const FLinearColor& Color) const;
	void DebugDrawBodyCoords(FPrimitiveDrawInterface* InPDI, const FTransform& BaseTransform, double Scale, UPhysicsAsset* PhysAsset, const TArray<FDebugBodyTfmInfo>& BodyTfms) const;
	
	void ResetDebugInfo();

	// Collection of all debug structures
	FDebugRelativeIKDrawInfo DebugDrawInfo;
	
	static UE_API FCriticalSection DebugDataMutex;
#endif
private:
	
	TObjectPtr<const UPhysicsAsset> SourcePhysicsAsset;
	TObjectPtr<const UPhysicsAsset> TargetPhysicsAsset;
	
	TObjectPtr<const UIKRigDefinition> TargetIKRig;
	
	// For runtime smoothing
	TArray<FVector3f> SmoothedPoints;

	// Cache notify-related data for faster weighted goal computations
	TObjectPtr<const UAnimSequence> CacheSourceAnimSequence;
	TObjectPtr<const URelativeBodyBakeAnimNotify> CachedNotifyInfo;
	float AnimSeqPlayHead;
	float SampleRate;

	// Cache source/target bone-names
	TArray<FName> SourceBoneNames;
	TArray<FName> TargetBoneNames;

	// Array of body-vert indices affecting each joint goal (for weighted averaging)
	TArray<TArray<int32>> CacheSourceBodyEffectVertIdx;
	TArray<FName> CacheSourceEffectBones;
	TArray<FName> CacheSourceDomainBones;
	TArray<double> FrameBoneWeights;
	TArray<FVector> FrameBoneTargets;

	// Cache skel and IK goal info
	TArray<FTransform> CacheSourceBoneInitTfm;
	TArray<FTransform> CacheTargetBoneInitTfm;
	TMap<FName,FTransform> CacheMapBoneSourceTargetTfm;
	// TMap<FName,FName> CacheIKBoneGoalMap;
	TMap<FName, const FResolvedBoneChain*> CacheBoneChains;
	TMap<FName,int32> CacheSourceSkelIndices;
	TMap<FName,int32> CacheTargetSkelIndices;
	// Additional per-frame ik cache maps for length lookup
	TMap<FName,double> CacheChainLengthMap;
	TMap<FName,FVector> CacheChainStartMap;

	// Cached montage instance/segment info for early-out matching in preupdate
	const FAnimMontageInstance* MontageInstance;
	float SegmentStartTime;
	float SegmentEndTime;
};

/* The blueprint/python API for editing a Stride Warping Op */
UCLASS(MinimalAPI, BlueprintType)
class UIKRetargetRelativeIKController : public UIKRetargetOpControllerBase
{
	GENERATED_BODY()
	
public:

	/* Get the current op settings as a struct.
	 * @return FIKRetargetRelativeIKOpSettings struct with the current settings used by the op. */
	UFUNCTION(BlueprintCallable, Category = Settings)
	UE_API FIKRetargetRelativeIKOpSettings GetSettings();

	/* Set the solver settings. Input is a custom struct type for this solver.
	 * @param InSettings a FIKRetargetRelativeIKOpSettings struct containing all the settings to apply to this op */
	UFUNCTION(BlueprintCallable, Category = Settings)
	UE_API void SetSettings(FIKRetargetRelativeIKOpSettings InSettings);
};

#undef LOCTEXT_NAMESPACE

#undef UE_API

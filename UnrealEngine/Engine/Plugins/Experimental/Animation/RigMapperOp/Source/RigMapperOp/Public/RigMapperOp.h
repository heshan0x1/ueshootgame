// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Retargeter/IKRetargetOps.h"
#include "StructUtils/InstancedStruct.h"
#include "Animation/AnimCurveTypes.h"
#include "RigMapperProcessor.h"

#include "RigMapperOp.generated.h"

#define UE_API RIGMAPPEROP_API

#define LOCTEXT_NAMESPACE "RigMapperOp"

class URigMapperDefinitionUserData;
class URigMapperDefinition;
class USkeletalMesh;
class USkeletalMeshComponent;

USTRUCT(BlueprintType, meta = (DisplayName = "RigMapper Settings"))
struct FIKRetargetRigMapperOpSettings : public FIKRetargetOpSettingsBase
{
	GENERATED_BODY()

	// Whether to copy all curves over to the target animation instance
	// NOTE: This setting also applies when exporting retargeted animations.
	// True: all source curves are copied to the target animation instance/asset
	// False: only remapped curves are copied on the target animation instance/asset
	// In general, we should set this to true if the source and target rig are the same,
	// and the RigMapper covers only a subset of the controls, but false otherwise.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Copy Curves")
	bool bCopyAllSourceCurves = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = RigMapper)
	TArray<TObjectPtr<URigMapperDefinition>> Definitions;

	UE_API virtual const UClass* GetControllerType() const override;
	
	UE_API virtual void CopySettingsAtRuntime(const FIKRetargetOpSettingsBase* InSettingsToCopyFrom) override;
};

USTRUCT(BlueprintType, meta = (DisplayName = "RigMapper"))
struct FIKRetargetRigMapperOp : public FIKRetargetOpBase
{
	GENERATED_BODY()
	
	
	virtual bool Initialize(
		const FIKRetargetProcessor& InProcessor,
		const FRetargetSkeleton& InSourceSkeleton,
		const FTargetSkeleton& InTargetSkeleton,
		const FIKRetargetOpBase* InParentOp,
		FIKRigLogger& Log) override;
	
	// NOTE: this op does not do anything in Run().
	virtual void Run(
		FIKRetargetProcessor& InProcessor,
		const double InDeltaTime,
		const TArray<FTransform>& InSourceGlobalPose,
		TArray<FTransform>& OutTargetGlobalPose) override {};

	virtual FIKRetargetOpSettingsBase* GetSettings() override
	{
		return &Settings;
	};

	virtual void SetSettings(const FIKRetargetOpSettingsBase* InSettings) override
	{
		Settings = *reinterpret_cast<const FIKRetargetRigMapperOpSettings*>(InSettings);
	};
	
	virtual const UScriptStruct* GetSettingsType() const override
	{
		return FIKRetargetRigMapperOpSettings::StaticStruct();
	}
	
	virtual const UScriptStruct* GetType() const override
	{
		return FIKRetargetRigMapperOp::StaticStruct();
	}

	/* RigMapperOp is a singleton because it can override definitions from the Target SkelMesh user data and therefore we should only do that once*/
	virtual bool IsSingleton() const override { return true; };

	virtual bool HasCurveProcessing() const override 
	{ 
		return true; 
	}

	virtual void ProcessAnimSequenceCurves(FIKRetargetOpBase::FCurveData InCurveMetaData, FIKRetargetOpBase::FFrameValues InCurveFrameValues,
		FIKRetargetOpBase::FCurveData& OutCurveMetaData, FIKRetargetOpBase::FFrameValues& OutCurveFrameValues)const override;


	UE_API virtual void AnimGraphPreUpdateMainThread(USkeletalMeshComponent& SourceMeshComponent, USkeletalMeshComponent& TargetMeshComponent) override;

	UE_API virtual void AnimGraphEvaluateAnyThread(FPoseContext& Output) override;


	UPROPERTY()
	FIKRetargetRigMapperOpSettings Settings;
	
	// cached curves, copied on the game thread in PreUpdate()
	FBlendedHeapCurve SourceCurves;


private:
	bool InitializeRigMapping(const USkeletalMesh* InTargetMesh);
	void EvaluateRigMapping(const FBlendedCurve& InCurve, FBlendedCurve& OutCurve);

	// The definitions that we have loaded. Cached to check against changes and reinit if need be
	TArray<TObjectPtr<URigMapperDefinition>> LoadedDefinitions;

	// The asset user data currently used to override definitions, if any was set on the skeletal mesh
	TObjectPtr<URigMapperDefinitionUserData> LoadedUserData;

	// The processor to evaluate the rig mapping
	FRigMapperProcessor RigMapperProcessor;

	// The cached input values passed to the rig mapper processor to avoid reallocations
	FRigMapperProcessor::FPoseValues CachedInputValues;

	// The cached output values passed to the rig mapper processor to avoid reallocations
	FRigMapperProcessor::FPoseValues CachedOutputValues;

	// Base curve mapping for bulk get/set of the linked pose curves
	struct FRigMapperCurveMapping
	{
		FRigMapperCurveMapping() = default;

		FRigMapperCurveMapping(FName InName, int32 InCurveIndex)
			: Name(InName)
			, CurveIndex(InCurveIndex)
		{}

		FName Name = NAME_None;
		int32 CurveIndex = INDEX_NONE;
	};

	// Curve mapping for bulk set of the curves with a mapping to the matching input
	struct FRigMapperOutputCurveMapping : FRigMapperCurveMapping
	{
		FRigMapperOutputCurveMapping() = default;

		FRigMapperOutputCurveMapping(FName InName, int32 InOutputCurveIndex, int32 InInputCurveIndex)
			: FRigMapperCurveMapping(InName, InOutputCurveIndex)
			, InputCurveIndex(InInputCurveIndex)
		{}

		int32 InputCurveIndex = INDEX_NONE;
	};

	// Input curve mapping for bulk get of the curve values
	using FInputCurveMappings = UE::Anim::TNamedValueArray<FDefaultAllocator, FRigMapperCurveMapping>;
	FInputCurveMappings InputCurveMappings;

	// Output curve mapping for the bulk get of the curve values
	using FOutputCurveMappings = UE::Anim::TNamedValueArray<FDefaultAllocator, FRigMapperOutputCurveMapping>;
	FOutputCurveMappings OutputCurveMappings;

};

/* The blueprint/python API for editing a RigMapper Op */
UCLASS(MinimalAPI, BlueprintType)
class UIKRetargetRigMapperOpController : public UIKRetargetOpControllerBase
{
	GENERATED_BODY()
	
public:
	/* Get the current op settings as a struct.
	 * @return FIKRetargetRigMapperOpSettings struct with the current settings used by the op. */
	UFUNCTION(BlueprintCallable, Category = Settings)
	UE_API FIKRetargetRigMapperOpSettings GetSettings();

	/* Set the op settings. Input is a custom struct type for this op.
	 * @param InSettings a FIKRetargetRigMapperOpSettings struct containing all the settings to apply to this op */
	UFUNCTION(BlueprintCallable, Category = Settings)
	UE_API void SetSettings(FIKRetargetRigMapperOpSettings InSettings);
};


#undef LOCTEXT_NAMESPACE

#undef UE_API

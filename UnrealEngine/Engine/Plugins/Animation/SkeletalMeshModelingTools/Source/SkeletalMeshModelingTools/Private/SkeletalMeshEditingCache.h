// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SkeletalMeshNotifier.h"
#include "Animation/DebugSkelMeshComponent.h"
#include "Changes/ValueWatcher.h"
#include "SkeletalMesh/SkeletalMeshToolsHelper.h"
#include "TargetInterfaces/MeshTargetInterfaceTypes.h"

#include "SkeletalMeshEditingCache.generated.h"

#define UE_API SKELETALMESHMODELINGTOOLS_API

class AActor;
class USkeletalMeshBackedDynamicMeshComponent;
class UPreviewMesh;
class USkeletalMeshComponent;
class URefSkeletonPoser;
class FPrimitiveDrawInterface;
struct FSkelDebugDrawConfig;
class FEditorViewportClient;
class FViewport;
class FSceneView;
class FCanvas;

class USkeletalMeshEditingCache;
class FSkeletalMeshEditingCacheNotifier: public ISkeletalMeshNotifier
{
public:
	FSkeletalMeshEditingCacheNotifier(USkeletalMeshEditingCache* InEditingCahe);
	virtual void HandleNotification(const TArray<FName>& BoneNames, const ESkeletalMeshNotifyType InNotifyType) override;
private:
	TWeakObjectPtr<USkeletalMeshEditingCache> EditingCache;
};


UCLASS(MinimalAPI, Transient)
class USkeletalMeshEditingCache: public UObject
{
	GENERATED_BODY()
	
public:
	DECLARE_DELEGATE_OneParam(FToggleSkeletalMeshBoneManipulation, bool);
	DECLARE_DELEGATE_RetVal(bool, FIsSkeletalMeshBoneManipulationEnabled);
	struct FDelegates
	{
		FToggleSkeletalMeshBoneManipulation ToggleSkeletalMeshBoneManipulationDelegate;
		FIsSkeletalMeshBoneManipulationEnabled IsSkeletalMeshBoneManipulationEnabledDelegate;
	};

	void Spawn(UWorld* World, USkeletalMeshComponent* SkeletalMeshComponent, EMeshLODIdentifier LOD, const FDelegates& InDelegates);
	EMeshLODIdentifier GetLOD() const;

	void Destroy();

	void ApplyChanges();
	bool IsApplyingChanges() const;
	void DiscardChanges();

	void Tick();
	bool HandleClick(HHitProxy *HitProxy);
	void Render(FPrimitiveDrawInterface* PDI, TFunction<void(FSkelDebugDrawConfig&)> OverrideBoneDrawConfigFunc);
	void DrawHUD(FEditorViewportClient* ViewportClient, FViewport* Viewport, const FSceneView* View, FCanvas* Canvas);

	TSharedPtr<ISkeletalMeshNotifier> GetNotifier();
	void HandleNotification(const TArray<FName>& BoneNames, const ESkeletalMeshNotifyType InNotifyType);

	
	USkeletalMeshBackedDynamicMeshComponent* GetEditingMeshComponent() const;	
	const TArray<FTransform>& GetComponentSpaceBoneTransforms() const;
	FTransform GetTransform();

	void HideSkeleton();
	void ShowSkeleton();
	void ToggleBoneManipulation(bool bEnable);
	bool IsDynamicMeshSkeletonEnabled() const;
	bool IsDynamicMeshBoneManipulationEnabled() const;
	int32 GetFirstSelectedBoneIndex() const;
	TArray<FName> GetSelectedBones() const;
	void ResetDynamicMeshBoneTransforms(bool bSelectedOnly);
	
	TArray<FName> GetMorphTargets() const;
	TMap<FName, float> GetMorphTargetWeights() const;
	float GetMorphTargetWeight(FName MorphTarget) const;

	void HandleSetMorphTargetWeight(FName MorphTarget, float Weight);
	bool GetMorphTargetAutoFill(FName MorphTarget);
	void HandleSetMorphTargetAutoFill(FName MorphTarget, bool bAutoFill, float PreviousOverrideWeight);
	void HandleMorphTargetEdited(FName MorphTarget);

	void OverrideMorphTargetWeight(FName MorphTarget, float Weight);
	void ClearMorphTargetOverride(FName MorphTarget);

	FName AddMorphTarget(FName InName);
	FName RenameMorphTarget(FName InOldName, FName InNewName);
	void RemoveMorphTargets(const TArray<FName>& InNames);
	TArray<FName> DuplicateMorphTargets(const TArray<FName>& InNames);
	
	URefSkeletonPoser* GetSkeletonPoser() const;
	
protected:
	UPROPERTY()
	TObjectPtr<AActor> HostActor;

	UPROPERTY()
	TObjectPtr<USkeletalMeshBackedDynamicMeshComponent> EditingMeshComponent;

	UPROPERTY()
	TObjectPtr<UPreviewMesh> PreviewMesh;

	UPROPERTY()
	TWeakObjectPtr<USkeletalMeshComponent> SkeletalMeshComponent;

	FDelegates Delegates;

	// Flags that drives both dyna mesh and skeletal mesh
	bool bCacheVisibility = true;
	ESkeletonDrawMode CacheSkeletonDrawMode = ESkeletonDrawMode::Default;
	bool bCacheBoneManipulation = true;

	bool bEnableDynamicMesh = false;
	bool bEnableDynamicMeshSkeleton = false;
	
	TValueWatcher<int32> ChangeCountWatcher;
	TValueWatcher<int32> SkeletonChangeCountWatcher;

	struct FMeshVisibilityState
	{
		bool bEnableDynamicMesh= false;
		bool bCacheVisibility = true;
		friend bool operator==(const FMeshVisibilityState&, const FMeshVisibilityState&) = default;
	};

	TValueWatcher<FMeshVisibilityState> MeshVisibilityWatcher;
	
	struct FSkeletonVisibilityState
	{
		bool bEnableDynamicMeshSkeleton = false;
		ESkeletonDrawMode CacheSkeletonDrawMode = ESkeletonDrawMode::Default;
		friend bool operator==(const FSkeletonVisibilityState&, const FSkeletonVisibilityState&) = default;
	};
	
	TValueWatcher<FSkeletonVisibilityState> SkeletonVisibilityWatcher;

	struct FBoneManipulationState
	{
		bool bEnableDynamicMeshSkeleton = false;
		bool bCacheBoneManipulation = true;
		friend bool operator==(const FBoneManipulationState&, const FBoneManipulationState&) = default;
	};

	
	TValueWatcher<FBoneManipulationState> BoneManipulationWatcher;
	
	TValueWatcher<bool> PreviewMeshVisibilityWatcher;

	void HandleVisibilityChangeRequest(bool bVisible);

	UPROPERTY()
	TObjectPtr<URefSkeletonPoser> RefSkeletonPoser;

	UPROPERTY()
	TMap<FName, float> MorphTargetOverrides;

	SkeletalMeshToolsHelper::FPoseChangeDetector PoseChangeDetector;
	
	void HandlePoseChangeDetectorEvent(SkeletalMeshToolsHelper::FPoseChangeDetector::FPayload Payload);

	const TArray<FTransform>& GetComponentSpaceBoneTransformsRefPose() const;

	const TMap<FName, float>& GetSkeletalMeshComponentMorphTargetWeights() const;

	void DeformPreviewMesh(const TArray<FTransform>& ComponentSpaceTransforms, const TMap<FName, float>& MorphTargetWeights);
	
	TSharedPtr<FSkeletalMeshEditingCacheNotifier> Notifier;
	
	TArray<FName> SelectedBoneNames;
	TArray<int32> SelectedBoneIndices;
	void UpdateSelectedBoneIndices();

	
	ESkeletonDrawMode DynamicMeshSkeletonDrawMode = ESkeletonDrawMode::Hidden;
	
	void SetSkeletalMeshSkeletonDrawMode(ESkeletonDrawMode DrawMode) const;
	ESkeletonDrawMode GetCurrentSkeletonDrawMode() const;
	
	ESkeletonDrawMode SavedSkeletonDrawMode = ESkeletonDrawMode::Default;

	FLinearColor GetDefaultBoneColor(int32 BoneIndex) const;

	UDebugSkelMeshComponent* GetDebugSkelMeshComponent() const;

};


#undef UE_API

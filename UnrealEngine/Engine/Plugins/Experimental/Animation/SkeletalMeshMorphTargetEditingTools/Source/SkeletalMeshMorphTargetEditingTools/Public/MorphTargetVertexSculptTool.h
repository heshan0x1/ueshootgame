// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IMorphTargetEditingToolInterface.h"
#include "MeshVertexSculptTool.h"
#include "SkeletalMesh/SkeletalMeshEditingInterface.h"
#include "BaseTools/MultiSelectionMeshEditingTool.h"
#include "MorphTargetEditingToolProperties.h"
#include "MeshDescription.h"
#include "ReferenceSkeleton.h"
#include "SkeletalMesh/SkeletalMeshToolsHelper.h"

#include "MorphTargetVertexSculptTool.generated.h"

/**
 * MorphTarget Vertex Sculpt Tool Builder
 */
UCLASS()
class UMorphTargetVertexSculptToolBuilder : public UMeshVertexSculptToolBuilder
{
	GENERATED_BODY()

public:
	virtual bool CanBuildTool(const FToolBuilderState& SceneState) const override;
	virtual UMeshSurfacePointTool* CreateNewTool(const FToolBuilderState& SceneState) const override;
	virtual const FToolTargetTypeRequirements& GetTargetRequirements() const override;
};


/**
* MorphTarget Sculpt Tool
*/
UCLASS()
class UMorphTargetVertexSculptTool : 
	public UMeshVertexSculptTool,
	public ISkeletalMeshEditingInterface,
	public IMorphTargetEditingToolInterface
{
	GENERATED_BODY()

public:
	virtual FName GetEditingMorphTarget();
	virtual TMap<FName, float> GetMorphTargetWeights();
	virtual const TArray<FTransform>& GetComponentSpaceBoneTransforms();
	virtual void ToggleBoneManipulation(bool bEnable);


	// UMeshVertexSculptTool overrides
	virtual void Setup() override;
	virtual void Shutdown(EToolShutdownType ShutdownType) override;
	virtual void CommitResult(UBaseDynamicMeshComponent* Component, bool bModifiedTopology) override;
	virtual void OnTick(float DeltaTime) override;
	virtual void OnPropertyModified(UObject* PropertySet, FProperty* Property) override;
	virtual void OnBeginStroke(const FRay& WorldRay) override;
	virtual void OnEndStroke() override;

protected:
	friend class FMorphTargetVertexSculptNonSymmetricChange;
	virtual void RegisterBrushes() override;

	void HandlePoseChangeDetectorEvent(SkeletalMeshToolsHelper::FPoseChangeDetector::FPayload Payload);

	// IMorphTargetEditingTool overrides
	virtual void SetupCommonProperties(const TFunction<void(UMorphTargetEditingToolProperties*)>& InSetupFunction) override;
	
	// ISkeletalMeshEditingInterface
	virtual void HandleSkeletalMeshModified(const TArray<FName>& Payload, const ESkeletalMeshNotifyType InNotifyType) override;
	
	
	void PoseSculptMesh(const TArray<FTransform>& ComponentSpaceTransforms, const TMap<FName, float>& MorphTargetWeights);

	UPROPERTY()
	TObjectPtr<class UMorphTargetEditingToolProperties> EditorToolProperties;

	UPROPERTY()
	TWeakObjectPtr<USkeletalMeshEditorContextObjectBase> EditorContext = nullptr;

	TFunction<const FDynamicMesh3*()> GetMeshWithoutCurrentMorphFunc;
	FDynamicMesh3 MeshWithoutEditingMorph;
	
	SkeletalMeshToolsHelper::FPoseChangeDetector PoseChangeDetector;

	

	FDelegateHandle OnToolMeshChangedDelegate;

	UPROPERTY()
	TObjectPtr<UDynamicMesh> ToolDynamicMesh;
	
	TArray<FTransform> ComponentSpaceTransformsRefPose;
	
	FName ToolMorphTargetName;


	TArray<FTransform> DefaultComponentSpaceBoneTransforms;
	
	TArray<FTransform> ComponentSpaceTransformsForPosedMesh;
	TMap<FName, float> MorphTargetWeightsForPosedMesh;

	bool bFastDeformSculptMesh = false;
	bool bFullRefreshSculptMesh = false;
};






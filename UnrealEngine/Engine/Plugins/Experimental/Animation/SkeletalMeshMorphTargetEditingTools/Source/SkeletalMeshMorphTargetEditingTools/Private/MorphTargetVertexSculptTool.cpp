// Copyright Epic Games, Inc. All Rights Reserved.

#include "MorphTargetVertexSculptTool.h"

#include "ContextObjectStore.h"
#include "ToolBuilderUtil.h"
#include "InteractiveToolManager.h"
#include "InteractiveToolChange.h"
#include "ModelingToolTargetUtil.h"
#include "ToolTargetManager.h"
#include "StaticMeshAttributes.h"
#include "Engine/SkeletalMesh.h"
#include "Components/SkeletalMeshComponent.h"
#include "MeshDescriptionToDynamicMesh.h"
#include "DynamicMeshToMeshDescription.h"
#include "IPersonaEditorModeManager.h"
#include "PersonaModule.h"
#include "SkeletalMeshOperations.h"
#include "BaseGizmos/BrushStampIndicator.h"
#include "TargetInterfaces/DynamicMeshCommitter.h"
#include "TargetInterfaces/DynamicMeshProvider.h"
#include "TargetInterfaces/PrimitiveComponentBackedTarget.h"
#include "AnimationRuntime.h"
#include "EraseMorphTargetBrushOps.h"
#include "SkeletalMeshAttributes.h"
#include "DynamicMesh/DynamicVertexSkinWeightsAttribute.h"
#include "SkeletalMesh/SkeletalMeshToolsHelper.h"
#include "TargetInterfaces/MeshDescriptionProvider.h"
#include "TargetInterfaces/SkeletonProvider.h"
#include "DynamicMesh/MeshNormals.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MorphTargetVertexSculptTool)

#define LOCTEXT_NAMESPACE "MorphTargetVertexSculptTool"


bool UMorphTargetVertexSculptToolBuilder::CanBuildTool(const FToolBuilderState& SceneState) const
{
	USkeletalMeshEditorContextObjectBase* EditorContext = SceneState.ToolManager->GetContextObjectStore()->FindContext<USkeletalMeshEditorContextObjectBase>();
	if (!EditorContext)
	{
		return false;
	}

	if (EditorContext->GetEditingMorphTarget() == NAME_None)
	{
		return false;
	}
	
	return Super::CanBuildTool(SceneState);
}

UMeshSurfacePointTool* UMorphTargetVertexSculptToolBuilder::CreateNewTool(const FToolBuilderState& SceneState) const
{
	UMorphTargetVertexSculptTool* MorphTargetEditorTool = NewObject<UMorphTargetVertexSculptTool>(SceneState.ToolManager);
	MorphTargetEditorTool->SetWorld(SceneState.World);
	return MorphTargetEditorTool;
}

const FToolTargetTypeRequirements& UMorphTargetVertexSculptToolBuilder::GetTargetRequirements() const
{
	static FToolTargetTypeRequirements ToolRequirements({
		UMaterialProvider::StaticClass(),
		UDynamicMeshProvider::StaticClass(),
		UDynamicMeshCommitter::StaticClass(),
		USceneComponentBackedTarget::StaticClass(),
		USkeletonProvider::StaticClass(),
	});

	return ToolRequirements;
}

FName UMorphTargetVertexSculptTool::GetEditingMorphTarget()
{
	if (EditorContext.IsValid())
	{
		return EditorContext->GetEditingMorphTarget();
	}

	return {};	
}

TMap<FName, float> UMorphTargetVertexSculptTool::GetMorphTargetWeights()
{
	if (EditorContext.IsValid())
	{
		return EditorContext->GetMorphTargetWeights();
	}

	return {};	
}

const TArray<FTransform>& UMorphTargetVertexSculptTool::GetComponentSpaceBoneTransforms()
{
	if (EditorContext.IsValid())
	{
		return EditorContext->GetComponentSpaceBoneTransforms(GetTarget());
	}

	if (DefaultComponentSpaceBoneTransforms.IsEmpty())
	{
		ISkeletonProvider* SkeletonProvider = CastChecked<ISkeletonProvider>(GetTarget());
		SkeletonProvider->GetSkeleton().GetBoneAbsoluteTransforms(DefaultComponentSpaceBoneTransforms);
	}
	
	return DefaultComponentSpaceBoneTransforms;
}

void UMorphTargetVertexSculptTool::ToggleBoneManipulation(bool bEnable)
{
	if (EditorContext.IsValid())
	{
		EditorContext->ToggleBoneManipulation(bEnable);
	}
}

void UMorphTargetVertexSculptTool::HandlePoseChangeDetectorEvent(SkeletalMeshToolsHelper::FPoseChangeDetector::FPayload Payload)
{
	if (!AllowToolMeshUpdates())
	{
		return;
	}
	
	using namespace SkeletalMeshToolsHelper;
	if (Payload.CurrentState == FPoseChangeDetector::PoseStoppedChanging)
	{
		bFullRefreshSculptMesh = true;
	}
	else if (Payload.CurrentState == FPoseChangeDetector::PoseJustChanged ||
		Payload.CurrentState == FPoseChangeDetector::PoseChanged)
	{
		bFastDeformSculptMesh = true;
	}
}



void UMorphTargetVertexSculptTool::Setup()
{
	EditorContext = GetToolManager()->GetContextObjectStore()->FindContext<USkeletalMeshEditorContextObjectBase>();
	
	SetupMorphEditingToolCommon();
	
	// Setup Vertex Sculpt Tool
	Super::Setup();

	ViewProperties->MaterialMode = EMeshEditingMaterialModes::ExistingMaterial;

	ToolDynamicMesh = NewObject<UDynamicMesh>();

	ToolDynamicMesh->SetMesh(*GetSculptMesh());

	
	
	// Remove unused attributes to make undo/redo faster
	GetSculptMesh()->Attributes()->RemoveAllMorphTargetAttributes();
	GetSculptMesh()->Attributes()->RemoveAllSkinWeightsAttributes();
	
	constexpr bool bCopyNormals = false, bCopyColors = false, bCopyUVs = false, bCopyAttributes = false;
	MeshWithoutEditingMorph.Copy(*GetSculptMesh(), bCopyNormals, bCopyColors, bCopyUVs, bCopyAttributes);
	
	ToolMorphTargetName = GetEditingMorphTarget();
	
	const FReferenceSkeleton& RefSkeleton = CastChecked<ISkeletonProvider>(GetTarget())->GetSkeleton();
	RefSkeleton.GetBoneAbsoluteTransforms(ComponentSpaceTransformsRefPose);

	ToggleBoneManipulation(true);

	PoseChangeDetector.GetNotifier().AddUObject(this, &UMorphTargetVertexSculptTool::HandlePoseChangeDetectorEvent);
}

void UMorphTargetVertexSculptTool::RegisterBrushes()
{
	Super::RegisterBrushes();
	
	GetMeshWithoutCurrentMorphFunc = [this](){return &MeshWithoutEditingMorph;};

	// Had to hijack the EraseSculptLayer identifier from base mesh vertex sculpt tool for our erase morph target tool since it is the simplest way to get an icon for the tool.
	RegisterBrushType(
		(int32)EMeshVertexSculptBrushType::EraseSculptLayer,
		LOCTEXT("EraseSculptLayerBrushTypeName", "EraseSculptLayer"),
		MakeUnique<FLambdaMeshSculptBrushOpFactory>([this]() { return MakeUnique<FEraseMorphTargetBrushOp>(GetMeshWithoutCurrentMorphFunc);}),
		NewObject<UEraseMorphTargetBrushOpProps>(this));
}

void UMorphTargetVertexSculptTool::Shutdown(EToolShutdownType ShutdownType)
{
	Super::Shutdown(ShutdownType);

	ShutdownMorphEditingToolCommon();	
}

void UMorphTargetVertexSculptTool::CommitResult(UBaseDynamicMeshComponent* Component, bool bModifiedTopology)
{
	GetToolManager()->BeginUndoTransaction(LOCTEXT("MorphTargetSculptMeshToolTransactionName", "Sculpt Morph Target"));

	ToolDynamicMesh->ProcessMesh([&](const FDynamicMesh3& InMesh)
		{
			// Commit the tool mesh cache instead of the sculpt mesh
			UE::ToolTarget::CommitDynamicMeshUpdate(Target, InMesh, bModifiedTopology);
		});

	if (EditorContext.IsValid())
	{
		EditorContext->NotifyMorphTargetEdited();
	}
	
	GetToolManager()->EndUndoTransaction();	
}

void UMorphTargetVertexSculptTool::OnTick(float DeltaTime)
{
	Super::OnTick(DeltaTime);

	if (!InStroke())
	{
		const TArray<FTransform>& ComponentSpaceTransforms = GetComponentSpaceBoneTransforms();
		const TMap<FName, float>& MorphTargetWeights = GetMorphTargetWeights();
		
		PoseChangeDetector.CheckPose(ComponentSpaceTransforms, MorphTargetWeights);

		if (bFastDeformSculptMesh)
		{
			bFastDeformSculptMesh = false;
			
			PoseSculptMesh(ComponentSpaceTransforms, MorphTargetWeights);
		}

		if (bFullRefreshSculptMesh)
		{
			bFullRefreshSculptMesh = false;
			
			// Using a dummy mesh replacement change to trigger octree/normals/base mesh refresh
			TSharedPtr<FDynamicMesh3> DummySculptMesh = MakeShared<FDynamicMesh3>(*GetSculptMesh());	
			FMeshReplacementChange DummyChange(DummySculptMesh, DummySculptMesh);
			OnDynamicMeshComponentChanged(DynamicMeshComponent, &DummyChange, false);
		}
	}
}

void UMorphTargetVertexSculptTool::OnPropertyModified(UObject* PropertySet, FProperty* Property)
{
	Super::OnPropertyModified(PropertySet, Property);
}


/*
 * internal Change classes
 */

class FMorphTargetVertexSculptNonSymmetricChange : public FToolCommandChange
{
public:
	virtual void Apply(UObject* Object) override;
	virtual void Revert(UObject* Object) override;
};


void FMorphTargetVertexSculptNonSymmetricChange::Apply(UObject* Object)
{
	if (Cast<UMorphTargetVertexSculptTool>(Object))
	{
		Cast<UMorphTargetVertexSculptTool>(Object)->UndoRedo_RestoreSymmetryPossibleState(false);
	}
}
void FMorphTargetVertexSculptNonSymmetricChange::Revert(UObject* Object)
{
	if (Cast<UMorphTargetVertexSculptTool>(Object))
	{
		Cast<UMorphTargetVertexSculptTool>(Object)->UndoRedo_RestoreSymmetryPossibleState(true);
	}
}

void UMorphTargetVertexSculptTool::OnBeginStroke(const FRay& WorldRay)
{
	Super::OnBeginStroke(WorldRay);
}

class FMeshMorphTargetChange : public FMeshChange
{
public:
	FName MorphTargetName;
	TArray<int32> Vertices;
	TArray<FVector> OldDeltas;
	TArray<FVector> NewDeltas;

	FMeshMorphTargetChange() = default;
	
	/** Describes this change (for debugging) */
	virtual FString ToString() const override
	{
		return TEXT("Edited Morph Target");
	}

	virtual void ApplyChangeToMesh(UE::Geometry::FDynamicMesh3* Mesh, bool bRevert) const override
	{
		UE::Geometry::FDynamicMeshMorphTargetAttribute* MorphTargetAttribute = Mesh->Attributes()->GetMorphTargetAttribute(MorphTargetName);

		ParallelFor(Vertices.Num(), [&](int32 Index)
			{
				const TArray<FVector>& Deltas = bRevert ? OldDeltas : NewDeltas;
				MorphTargetAttribute->SetValue(Vertices[Index], Deltas[Index]); 		
			});
	};
};



void UMorphTargetVertexSculptTool::OnEndStroke()
{
	// update spatial
	bTargetDirty = true;

	GetActiveBrushOp()->EndStroke(GetSculptMesh(), LastStamp, VertexROI);

	check(ActiveVertexChange);

	TMap<FName, float> MorphTargetWeights = MorphTargetWeightsForPosedMesh;
	const TArray<FTransform>& ComponentSpaceTransforms = ComponentSpaceTransformsForPosedMesh; 

	float EditingMorphTargetWeight = MorphTargetWeights[ToolMorphTargetName];
	
	// Don't update the cache if the morph cannot be extracted from the mesh
	if (!FMath::IsNearlyZero(EditingMorphTargetWeight) && !ActiveVertexChange->Change->Vertices.IsEmpty())
	{
		FMeshMorphTargetChange* MorphTargetChange = new FMeshMorphTargetChange(); 
		MorphTargetChange->MorphTargetName = ToolMorphTargetName;
		MorphTargetChange->Vertices = MoveTemp(ActiveVertexChange->Change->Vertices);
		MorphTargetChange->OldDeltas.SetNumUninitialized(MorphTargetChange->Vertices.Num());
		MorphTargetChange->NewDeltas.SetNumUninitialized(MorphTargetChange->Vertices.Num());
		
		// Exclude the morph target we are trying to extract
		MorphTargetWeights.Remove(ToolMorphTargetName);
		
		using namespace SkeletalMeshToolsHelper;
		TArray<FMatrix> BoneMatrices = ComputeBoneMatrices(
				ComponentSpaceTransformsRefPose,
				ComponentSpaceTransforms
				);

		ToolDynamicMesh->EditMesh([&](FDynamicMesh3& Mesh)
			{
				FDynamicMeshMorphTargetAttribute* MorphTargetAttribute = Mesh.Attributes()->GetMorphTargetAttribute(ToolMorphTargetName);
				
				auto WriteFunction = [&](FVertInfo VertInfo, const FVector& UnposedVertPos)
					{
						int32 VertID = VertInfo.VertID;
						FVector NewDelta = UnposedVertPos - FVector(Mesh.GetVertex(VertID));
						NewDelta /= EditingMorphTargetWeight;
						
						FVector OldDelta;
						MorphTargetAttribute->GetValue(VertID, OldDelta);
						MorphTargetAttribute->SetValue(VertID, NewDelta);

						MorphTargetChange->OldDeltas[VertInfo.VertArrayIndex] = OldDelta;
						MorphTargetChange->NewDeltas[VertInfo.VertArrayIndex] = NewDelta;
					};

				GetUnposedMesh(WriteFunction, *GetSculptMesh(), Mesh, BoneMatrices, NAME_None, MorphTargetWeights, MorphTargetChange->Vertices);
			});
		
		
		TUniquePtr<TWrappedToolCommandChange<FMeshMorphTargetChange>> NewChange = MakeUnique<TWrappedToolCommandChange<FMeshMorphTargetChange>>();
		
		NewChange->WrappedChange.Reset(MorphTargetChange);
		NewChange->BeforeModify = [this](bool bRevert)
		{
			WaitForPendingUndoRedoUpdate();
		};

		
		NewChange->AfterModify = [this](bool bRevert)
		{
			bFastDeformSculptMesh = true;
			bFullRefreshSculptMesh = true;
		};

		GetToolManager()->EmitObjectChange(ToolDynamicMesh, MoveTemp(NewChange), LOCTEXT("VertexSculptChange", "Brush Stroke"));
		if (bMeshSymmetryIsValid && bApplySymmetry == false)
		{
			// if we end a stroke while symmetry is possible but disabled, we now have to assume that symmetry is no longer possible
			GetToolManager()->EmitObjectChange(this, MakeUnique<FMorphTargetVertexSculptNonSymmetricChange>(), LOCTEXT("DisableSymmetryChange", "Disable Symmetry"));
			bMeshSymmetryIsValid = false;
			SymmetryProperties->bSymmetryCanBeEnabled = bMeshSymmetryIsValid;
		}
	}
	
	
	LongTransactions.Close(GetToolManager());

	delete ActiveVertexChange;
	ActiveVertexChange = nullptr;
}

void UMorphTargetVertexSculptTool::SetupCommonProperties(const TFunction<void(UMorphTargetEditingToolProperties*)>& InSetupFunction)
{
	EditorToolProperties = NewObject<UMorphTargetEditingToolProperties>(this);
	EditorToolProperties->SetFlags(RF_Transactional);

	InSetupFunction(EditorToolProperties);
	
	AddToolPropertySource(EditorToolProperties);
}

void UMorphTargetVertexSculptTool::HandleSkeletalMeshModified(const TArray<FName>& Payload, const ESkeletalMeshNotifyType InNotifyType)
{
}

void UMorphTargetVertexSculptTool::PoseSculptMesh(const TArray<FTransform>& ComponentSpaceTransforms, const TMap<FName, float>& MorphTargetWeights)
{
	using namespace SkeletalMeshToolsHelper;
	
	// have to wait for any outstanding stamp/undo update to finish...
	WaitForPendingStampUpdateConst();
	WaitForPendingUndoRedoUpdate();

	TArray<FMatrix> BoneMatrices = ComputeBoneMatrices(ComponentSpaceTransformsRefPose, ComponentSpaceTransforms);

	FDynamicMesh3& SculptMesh = *GetSculptMesh();

	ToolDynamicMesh->ProcessMesh([&](const FDynamicMesh3& InMesh)
		{
			FDynamicMeshMorphTargetAttribute* MorphTargetAttribute = InMesh.Attributes()->GetMorphTargetAttribute(ToolMorphTargetName);

			float EditingMorphTargetWeight = MorphTargetWeights[ToolMorphTargetName];
	
			auto WriteFunc = [&](int32 VertID, const FVector& PosedVertPos)
				{
					SculptMesh.SetVertex(VertID, PosedVertPos);

					FVector Delta;
					MorphTargetAttribute->GetValue(VertID, Delta);
			
					MeshWithoutEditingMorph.SetVertex(VertID,  PosedVertPos - (Delta * EditingMorphTargetWeight));
				};

			GetPosedMesh(WriteFunc, InMesh, BoneMatrices, NAME_None, MorphTargetWeights);
			FMeshNormals::QuickRecomputeOverlayNormals(SculptMesh);
			
			ComponentSpaceTransformsForPosedMesh = ComponentSpaceTransforms;
			MorphTargetWeightsForPosedMesh = MorphTargetWeights;
		

		});

	constexpr bool bNormal = true;
	DynamicMeshComponent->FastNotifyPositionsUpdated(bNormal);


}


#undef LOCTEXT_NAMESPACE


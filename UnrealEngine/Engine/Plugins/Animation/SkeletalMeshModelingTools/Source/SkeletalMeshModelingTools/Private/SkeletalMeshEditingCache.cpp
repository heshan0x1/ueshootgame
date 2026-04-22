// Copyright Epic Games, Inc. All Rights Reserved.

#include "SkeletalMeshEditingCache.h"

#include "CanvasItem.h"
#include "CanvasTypes.h"
#include "PreviewMesh.h"
#include "SkeletalDebugRendering.h"
#include "Components/SKMBackedDynaMeshComponent.h"
#include "Preferences/PersonaOptions.h"
#include "SkeletalMesh/RefSkeletonPoser.h"
#include "SkeletalMesh/SkeletalMeshEditingInterface.h"
#include "Animation/AnimInstance.h"
#include "DynamicMesh/MeshNormals.h"
#include "UnrealClient.h"
#include "EditorViewportClient.h"
#include "SceneView.h"
#include "Engine/Engine.h"


#include UE_INLINE_GENERATED_CPP_BY_NAME(SkeletalMeshEditingCache)

#define LOCTEXT_NAMESPACE "SkeletalMeshEditingCache"

namespace SkeletalMeshEditingCacheLocal
{
	static FIntPoint GetDPIUnscaledSize(FViewport* Viewport, FViewportClient* Client)
	{
		const FIntPoint Size = Viewport->GetSizeXY();
		const float DPIScale = Client->GetDPIScale();
		// (FIntPoint / float) implicitly casts the float to an int if you try to divide it directly
		return FIntPoint(static_cast<int32>(Size.X / DPIScale), static_cast<int32>(Size.Y / DPIScale));
	}
}

FSkeletalMeshEditingCacheNotifier::FSkeletalMeshEditingCacheNotifier(USkeletalMeshEditingCache* InEditingCahe)
	:EditingCache(InEditingCahe)
{
}

void FSkeletalMeshEditingCacheNotifier::HandleNotification(const TArray<FName>& BoneNames, const ESkeletalMeshNotifyType InNotifyType)
{
	if (Notifying())
	{
		return;
	}

	EditingCache->HandleNotification(BoneNames, InNotifyType);

	Notify(BoneNames, InNotifyType);
}

void USkeletalMeshEditingCache::Spawn(UWorld* World, USkeletalMeshComponent* InSkeletalMeshComponent, EMeshLODIdentifier InLOD, const FDelegates& InDelegates)
{
	Delegates = InDelegates;
	SkeletalMeshComponent = InSkeletalMeshComponent;
	
	HostActor = World->SpawnActor(AActor::StaticClass());
	EditingMeshComponent = NewObject<USkeletalMeshBackedDynamicMeshComponent>(HostActor);

	USkeletalMesh* SkeletalMesh = SkeletalMeshComponent->GetSkeletalMeshAsset();
	EditingMeshComponent->Init(SkeletalMesh, InLOD);

	TArray<UMaterialInterface*> Materials = SkeletalMeshComponent->GetMaterials();
	for (int k = 0; k < Materials.Num(); ++k)
	{
		EditingMeshComponent->SetMaterial(k, Materials[k]);
	}
	
	EditingMeshComponent->GetOnRequestingVisibilityChange().AddUObject(this, &USkeletalMeshEditingCache::HandleVisibilityChangeRequest);

	HostActor->AddInstanceComponent(EditingMeshComponent);
	HostActor->SetRootComponent(EditingMeshComponent);
	HostActor->RegisterAllComponents();

	FTransform ActorTransform = SkeletalMeshComponent->GetComponentTransform();
	
	HostActor->SetActorTransform(ActorTransform);
		
	FTransform PreviewActorTransform = ActorTransform ;
	
	PreviewMesh = NewObject<UPreviewMesh>();
	PreviewMesh->CreateInWorld(World ,PreviewActorTransform);

	UDynamicMesh* EditingDynamicMesh = EditingMeshComponent->GetDynamicMesh();
	EditingDynamicMesh->ProcessMesh([&](const FDynamicMesh3& ReadMesh)
	{
		FDynamicMesh3 PreviewMeshObject = ReadMesh;
		PreviewMesh->ReplaceMesh(MoveTemp(PreviewMeshObject));
	});

	PreviewMesh->SetMaterials(Materials);

	RefSkeletonPoser = NewObject<URefSkeletonPoser>();
	RefSkeletonPoser->SetRefSkeleton(EditingMeshComponent->GetRefSkeleton());
	
	EditingMeshComponent->SetVisibility(false);
	PreviewMesh->SetVisible(false);
	bEnableDynamicMesh = false;
	bEnableDynamicMeshSkeleton = false;

	bCacheVisibility = true;
	CacheSkeletonDrawMode = ESkeletonDrawMode::Default;
	bCacheBoneManipulation = true;

	SetSkeletalMeshSkeletonDrawMode(CacheSkeletonDrawMode);
	
	ChangeCountWatcher.Initialize(
		[this]()
			{
				return GetEditingMeshComponent()->GetChangeCount();
			},
		[this](int32 ChangeCount)
			{
				bEnableDynamicMesh = ChangeCount > 0;
				UDynamicMesh* DynamicMesh = EditingMeshComponent->GetDynamicMesh();
				FDynamicMesh3 PreviewMeshObject;
				DynamicMesh->ProcessMesh([&](const FDynamicMesh3& ReadMesh)
					{
						PreviewMeshObject.Copy(ReadMesh);
					});

				PreviewMesh->ReplaceMesh(MoveTemp(PreviewMeshObject));	
				
			},
		GetEditingMeshComponent()->GetChangeCount());

	SkeletonChangeCountWatcher.Initialize(
		[this]()
			{
				return GetEditingMeshComponent()->GetSkeletonChangeTracker().GetChangeCount();
			},
		[this](int32 SkeletonChangeCount)
			{
				bEnableDynamicMeshSkeleton = SkeletonChangeCount > 0;
				RefSkeletonPoser->SetRefSkeleton(GetEditingMeshComponent()->GetRefSkeleton());
				UpdateSelectedBoneIndices();
			},
		GetEditingMeshComponent()->GetSkeletonChangeTracker().GetChangeCount());

	MeshVisibilityWatcher.Initialize(
		[this]()
			{
				FMeshVisibilityState State;
				State.bEnableDynamicMesh = bEnableDynamicMesh;
				State.bCacheVisibility = bCacheVisibility;
				return State;
			},
		[this](FMeshVisibilityState State)
			{
				PreviewMesh->SetVisible(
					State.bEnableDynamicMesh && State.bCacheVisibility);

				if (SkeletalMeshComponent.IsValid())
				{
					SkeletalMeshComponent->SetVisibility(
						!State.bEnableDynamicMesh && State.bCacheVisibility);
				}
			},
		FMeshVisibilityState(bEnableDynamicMesh, bCacheVisibility)
	);

	SkeletonVisibilityWatcher.Initialize(
		[this]()
			{
				FSkeletonVisibilityState State;
				State.bEnableDynamicMeshSkeleton = bEnableDynamicMeshSkeleton;
				State.CacheSkeletonDrawMode = CacheSkeletonDrawMode;
				return State;
			},
		[this](FSkeletonVisibilityState State)
			{
				if (State.bEnableDynamicMeshSkeleton)
				{
					DynamicMeshSkeletonDrawMode = State.CacheSkeletonDrawMode;
					SetSkeletalMeshSkeletonDrawMode(ESkeletonDrawMode::Hidden);
				}
				else
				{
					DynamicMeshSkeletonDrawMode = ESkeletonDrawMode::Hidden;
					SetSkeletalMeshSkeletonDrawMode(State.CacheSkeletonDrawMode);
				}
			},
		FSkeletonVisibilityState(bEnableDynamicMeshSkeleton, CacheSkeletonDrawMode)
	);

	BoneManipulationWatcher.Initialize(
		[this]()
			{
				FBoneManipulationState State;
				State.bEnableDynamicMeshSkeleton = bEnableDynamicMeshSkeleton;
				State.bCacheBoneManipulation = bCacheBoneManipulation;
				return State;
			},
		[this](FBoneManipulationState State)
			{
				Delegates.ToggleSkeletalMeshBoneManipulationDelegate.ExecuteIfBound(
					!State.bEnableDynamicMeshSkeleton && bCacheBoneManipulation);
			},
		FBoneManipulationState(bEnableDynamicMeshSkeleton, bCacheBoneManipulation)
	);

	PreviewMeshVisibilityWatcher.Initialize(
		[this]()
			{
				return PreviewMesh->IsVisible();
			},
		[this](bool bIsVisible)
			{
				if (bIsVisible)
				{
					DeformPreviewMesh(GetComponentSpaceBoneTransforms(), GetMorphTargetWeights());
				}
			},
		PreviewMesh->IsVisible());

	PoseChangeDetector.GetNotifier().AddUObject(this, &USkeletalMeshEditingCache::HandlePoseChangeDetectorEvent);
}

EMeshLODIdentifier USkeletalMeshEditingCache::GetLOD() const
{
	return EditingMeshComponent->GetLOD();
}


void USkeletalMeshEditingCache::Destroy()
{
	HostActor->Destroy();
	PreviewMesh->Disconnect();
	
	if (SkeletalMeshComponent.IsValid())
	{
		SkeletalMeshComponent->SetVisibility(true);
	}
}

void USkeletalMeshEditingCache::ApplyChanges()
{


	if (SkeletalMeshComponent.IsValid())
	{
		// See USkeletalMeshComponentToolTarget
		
		// Unregister the component while we update its skeletal mesh
		FComponentReregisterContext ComponentReregisterContext(SkeletalMeshComponent.Get());

		EditingMeshComponent->CommitToSkeletalMesh();

		// this rebuilds physics, but it doesn't undo!
		SkeletalMeshComponent->RecreatePhysicsState();
	}
	else
	{
		EditingMeshComponent->CommitToSkeletalMesh();
	}
}

bool USkeletalMeshEditingCache::IsApplyingChanges() const
{
	return EditingMeshComponent->IsCommitting();
}

void USkeletalMeshEditingCache::DiscardChanges()
{
	EditingMeshComponent->DiscardChanges();
}

void USkeletalMeshEditingCache::Tick()
{
	ChangeCountWatcher.CheckAndUpdate();
	SkeletonChangeCountWatcher.CheckAndUpdate();
	MeshVisibilityWatcher.CheckAndUpdate();
	SkeletonVisibilityWatcher.CheckAndUpdate();
	BoneManipulationWatcher.CheckAndUpdate();
	PreviewMeshVisibilityWatcher.CheckAndUpdate();
	
	if (bEnableDynamicMesh)
	{
		PoseChangeDetector.CheckPose( GetComponentSpaceBoneTransforms(),  GetMorphTargetWeights());
	}
}

bool USkeletalMeshEditingCache::HandleClick(HHitProxy* HitProxy)
{
	if (bEnableDynamicMeshSkeleton)
	{
		TArray<FName> SelectedBones;	
		if (const HBoneHitProxy* BoneHitProxy = HitProxyCast<HBoneHitProxy>(HitProxy))
		{
			SelectedBones.Emplace(BoneHitProxy->BoneName);
		}

		Notifier->HandleNotification(SelectedBones, ESkeletalMeshNotifyType::BonesSelected);
	
		return true;
	}

	return false;
}

void USkeletalMeshEditingCache::Render(FPrimitiveDrawInterface* PDI, TFunction<void(FSkelDebugDrawConfig&)> OverrideBoneDrawConfigFunc)
{
	if (DynamicMeshSkeletonDrawMode == ESkeletonDrawMode::Hidden)
	{
		return;
	}

	const FReferenceSkeleton& RefSkeleton = EditingMeshComponent->GetRefSkeleton();
	const TArray<FTransform>& ComponentSpaceTransforms = GetComponentSpaceBoneTransforms();
	const FTransform& PreviewActorTransform = PreviewMesh->GetActor()->GetActorTransform();
	
	const int32 NumBones = RefSkeleton.GetRawBoneNum();	

	TArray<FTransform> WorldTransforms;
	WorldTransforms.AddUninitialized(NumBones);

	TArray<FLinearColor> BoneColors;
	BoneColors.AddUninitialized(NumBones);

	TArray<FBoneIndexType> RequiredBones;
	RequiredBones.AddUninitialized(NumBones);
	
	for ( int32 BoneIndex=0; BoneIndex<NumBones; ++BoneIndex )
	{
		RequiredBones[BoneIndex] = BoneIndex;
		WorldTransforms[BoneIndex] = ComponentSpaceTransforms[BoneIndex] * PreviewActorTransform ;
		BoneColors[BoneIndex] = GetDefaultBoneColor(BoneIndex);
	}

	constexpr bool bForceDraw = false;
	constexpr bool bAddHitProxy = true;
		
	const bool bUseMultiColors = GetDefault<UPersonaOptions>()->bShowBoneColors;
	
	FSkelDebugDrawConfig DrawConfig;
	DrawConfig.BoneDrawMode = EBoneDrawMode::Type::All ;
	DrawConfig.BoneDrawSize = 1.0f;
	DrawConfig.bAddHitProxy = bAddHitProxy;
	DrawConfig.bForceDraw = bForceDraw;
	DrawConfig.bUseMultiColorAsDefaultColor = bUseMultiColors;
	DrawConfig.DefaultBoneColor = GetMutableDefault<UPersonaOptions>()->DefaultBoneColor;
	DrawConfig.AffectedBoneColor = GetMutableDefault<UPersonaOptions>()->AffectedBoneColor;
	DrawConfig.SelectedBoneColor = GetMutableDefault<UPersonaOptions>()->SelectedBoneColor;
	DrawConfig.ParentOfSelectedBoneColor = GetMutableDefault<UPersonaOptions>()->ParentOfSelectedBoneColor;

	OverrideBoneDrawConfigFunc(DrawConfig);

	TArray<TRefCountPtr<HHitProxy>> HitProxies; HitProxies.Reserve(NumBones);

	for (int32 Index = 0; Index < NumBones; ++Index)
	{
		HitProxies.Add(new HBoneHitProxy(Index, RefSkeleton.GetBoneName(Index)));
	}

	SkeletalDebugRendering::DrawBones(
		PDI,
		PreviewActorTransform.GetLocation(),
		RequiredBones,
		RefSkeleton,
		WorldTransforms,
		SelectedBoneIndices,
		BoneColors,
		HitProxies,
		DrawConfig
	);
}

void USkeletalMeshEditingCache::DrawHUD(FEditorViewportClient* ViewportClient, FViewport* Viewport, const FSceneView* View, FCanvas* Canvas)
{
	if (bEnableDynamicMeshSkeleton)
	{
		// See FSkeletonSelectionEditMode::DrawHUD
		const FReferenceSkeleton& RefSkeleton = GetEditingMeshComponent()->GetRefSkeleton();
		const int32 BoneIndex = GetFirstSelectedBoneIndex();

		// Draw name of selected bone
		if (RefSkeleton.IsValidIndex(BoneIndex))
		{
			const FIntPoint ViewPortSize = SkeletalMeshEditingCacheLocal::GetDPIUnscaledSize(Viewport, ViewportClient);
			const int32 HalfX = ViewPortSize.X / 2;
			const int32 HalfY = ViewPortSize.Y / 2;

			const FName BoneName = RefSkeleton.GetBoneName(BoneIndex);

			const FMatrix BoneMatrix = (GetComponentSpaceBoneTransforms()[BoneIndex] * GetTransform()).ToMatrixNoScale();
			const FPlane Proj = View->Project(BoneMatrix.GetOrigin());
			if (Proj.W > 0.f)
			{
				const int32 XPos = HalfX + static_cast<int32>(HalfX * Proj.X);
				const int32 YPos = HalfY + static_cast<int32>(HalfY * Proj.Y * -1);

				FCanvasTextItem TextItem(FVector2D(XPos, YPos), FText::FromString(BoneName.ToString()), GEngine->GetSmallFont(), FLinearColor::White);
				TextItem.EnableShadow(FLinearColor::Black);
				Canvas->DrawItem(TextItem);
			}
		}
	}
}

TSharedPtr<ISkeletalMeshNotifier> USkeletalMeshEditingCache::GetNotifier()
{
	if (!Notifier.IsValid())
	{
		Notifier = MakeShared<FSkeletalMeshEditingCacheNotifier>(this);
	}

	return Notifier;
}

void USkeletalMeshEditingCache::HandleNotification(const TArray<FName>& BoneNames, const ESkeletalMeshNotifyType InNotifyType)
{
	switch (InNotifyType)
	{
	case ESkeletalMeshNotifyType::BonesSelected:
		SelectedBoneNames = BoneNames;
		UpdateSelectedBoneIndices();
		break;
	}
}


USkeletalMeshBackedDynamicMeshComponent* USkeletalMeshEditingCache::GetEditingMeshComponent() const
{
	return EditingMeshComponent;
}


void USkeletalMeshEditingCache::HandleVisibilityChangeRequest(bool bVisible)
{
	bCacheVisibility = bVisible;
}



void USkeletalMeshEditingCache::HandlePoseChangeDetectorEvent(SkeletalMeshToolsHelper::FPoseChangeDetector::FPayload Payload)
{
	using namespace SkeletalMeshToolsHelper;
	
	if (Payload.CurrentState == FPoseChangeDetector::PoseJustChanged ||
		Payload.CurrentState == FPoseChangeDetector::PoseChanged)
	{
		DeformPreviewMesh(Payload.ComponentSpaceTransforms, Payload.MorphTargetWeights);
	}
}

const TArray<FTransform>& USkeletalMeshEditingCache::GetComponentSpaceBoneTransforms() const
{
	if (bEnableDynamicMeshSkeleton)
	{
		return RefSkeletonPoser->GetComponentSpaceTransforms();
	}

	return SkeletalMeshComponent->GetComponentSpaceTransforms();
}

FTransform USkeletalMeshEditingCache::GetTransform()
{
	return PreviewMesh->GetTransform();
}

void USkeletalMeshEditingCache::HideSkeleton()
{
	SavedSkeletonDrawMode = CacheSkeletonDrawMode;
	CacheSkeletonDrawMode = ESkeletonDrawMode::Hidden;
}

void USkeletalMeshEditingCache::ShowSkeleton()
{
	CacheSkeletonDrawMode = SavedSkeletonDrawMode;
}

void USkeletalMeshEditingCache::ToggleBoneManipulation(bool bEnable)
{
	bCacheBoneManipulation = bEnable;
}


bool USkeletalMeshEditingCache::IsDynamicMeshSkeletonEnabled() const
{
	return bEnableDynamicMeshSkeleton;
}

bool USkeletalMeshEditingCache::IsDynamicMeshBoneManipulationEnabled() const
{
	return IsDynamicMeshSkeletonEnabled() && bCacheBoneManipulation;
}

int32 USkeletalMeshEditingCache::GetFirstSelectedBoneIndex() const
{
	return !SelectedBoneIndices.IsEmpty() ? SelectedBoneIndices[0] : INDEX_NONE;
}

TArray<FName> USkeletalMeshEditingCache::GetSelectedBones() const
{
	return SelectedBoneNames;
}

void USkeletalMeshEditingCache::ResetDynamicMeshBoneTransforms(bool bSelectedOnly)
{
	if (!bEnableDynamicMeshSkeleton)
	{
		return;
	}
	
	RefSkeletonPoser->BeginPoseChange();
	if (bSelectedOnly)
	{
		for (int32 BoneIndex : SelectedBoneIndices)
		{
			RefSkeletonPoser->ClearBoneAdditiveTransform(BoneIndex);
		}
	}
	else
	{
		RefSkeletonPoser->ClearAllBoneAdditiveTransforms();
	}
	RefSkeletonPoser->EndPoseChange();
}

TArray<FName> USkeletalMeshEditingCache::GetMorphTargets() const
{
	return EditingMeshComponent->GetMorphTargetChangeTracker().GetCurrentMorphTargetNames();
}

TMap<FName, float> USkeletalMeshEditingCache::GetMorphTargetWeights() const
{
	TMap<FName, float> MorphTargetWeights;

	for (const FName& Name : GetMorphTargets())
	{
		MorphTargetWeights.Emplace(Name, 0.0f);
	}
	
	const TMap<FName, float>& SkeletalMeshWeights = GetSkeletalMeshComponentMorphTargetWeights();

	for (const TPair<FName, float>& SkeletalMeshWeight : SkeletalMeshWeights)
	{
		FName UpdatedName = EditingMeshComponent->GetMorphTargetChangeTracker().GetCurrentMorphTargetName(SkeletalMeshWeight.Key);
		if (UpdatedName != NAME_None)
		{
			MorphTargetWeights[UpdatedName] = SkeletalMeshWeight.Value;
		}
	}

	MorphTargetWeights.Append(MorphTargetOverrides);

	return MorphTargetWeights;	
}

float USkeletalMeshEditingCache::GetMorphTargetWeight(FName MorphTarget) const
{
	if (const float* Weight = MorphTargetOverrides.Find(MorphTarget))
	{
		return *Weight;
	}

	const TMap<FName, float>& SkeletalMeshWeights = GetSkeletalMeshComponentMorphTargetWeights();

	FName OriginalName = EditingMeshComponent->GetMorphTargetChangeTracker().GetOriginalMorphTargetName(MorphTarget);
	
	if (const float* Weight = SkeletalMeshWeights.Find(OriginalName))
	{
		return *Weight;
	}

	return 0.0f;
}


void USkeletalMeshEditingCache::HandleSetMorphTargetWeight(FName MorphTarget, float Weight)
{
	if (MorphTarget == NAME_None)
	{
		return;
	}

	if (GUndo)
	{
		SetFlags(RF_Transactional);
		Modify();
	}
	
	MorphTargetOverrides.Emplace(MorphTarget, Weight);
	
	FName OriginalMorphTargetName = EditingMeshComponent->GetMorphTargetChangeTracker().GetOriginalMorphTargetName(MorphTarget);
	if (OriginalMorphTargetName != NAME_None)
	{
		if (UDebugSkelMeshComponent* Mesh = GetDebugSkelMeshComponent())
		{
			if (GUndo)
			{
				Mesh->SetFlags(RF_Transactional);
				Mesh->Modify();
			}
			constexpr bool bRemoveZeroWeight = false;
			Mesh->SetMorphTarget(OriginalMorphTargetName, Weight, bRemoveZeroWeight);
		}
	}
}

bool USkeletalMeshEditingCache::GetMorphTargetAutoFill(FName Name)
{
	return !MorphTargetOverrides.Contains(Name);
}

void USkeletalMeshEditingCache::HandleSetMorphTargetAutoFill(FName Name, bool bAutoFill, float PreviousOverrideWeight)
{
	if (GUndo)
	{
		SetFlags(RF_Transactional);
		Modify();
	}
	
	if (bAutoFill)
	{
		MorphTargetOverrides.Remove(Name);
	}
	else
	{
		MorphTargetOverrides.Emplace(Name, PreviousOverrideWeight);
	}

	if (UDebugSkelMeshComponent* Mesh = GetDebugSkelMeshComponent())
	{
		FName OriginalMorphTargetName = EditingMeshComponent->GetMorphTargetChangeTracker().GetOriginalMorphTargetName(Name);
		if (OriginalMorphTargetName != NAME_None)
		{
			if (GUndo)
			{
				Mesh->SetFlags(RF_Transactional);
				Mesh->Modify();
			}

			if (bAutoFill)
			{
				constexpr bool bRemoveZeroWeight = true;
				Mesh->SetMorphTarget(OriginalMorphTargetName, 0.0f, bRemoveZeroWeight);	
			}
			else
			{
				constexpr bool bRemoveZeroWeight = false;
				Mesh->SetMorphTarget(OriginalMorphTargetName, MorphTargetOverrides[Name], bRemoveZeroWeight);
			}
		}
	}
}

void USkeletalMeshEditingCache::HandleMorphTargetEdited(FName MorphTarget)
{
	EditingMeshComponent->MarkMorphTargetEdited(MorphTarget);	
}

void USkeletalMeshEditingCache::OverrideMorphTargetWeight(FName MorphTarget, float Weight)
{
	HandleSetMorphTargetWeight(MorphTarget, Weight);
}

void USkeletalMeshEditingCache::ClearMorphTargetOverride(FName MorphTarget)
{
	if (MorphTarget != NAME_None)
	{
		constexpr bool bAutoFill = true;
		constexpr float DummyWeight = 0.0f;
		HandleSetMorphTargetAutoFill(MorphTarget, bAutoFill, DummyWeight);
	}
}

FName USkeletalMeshEditingCache::AddMorphTarget(FName InName)
{
	return EditingMeshComponent->AddMorphTarget(InName);
}

FName USkeletalMeshEditingCache::RenameMorphTarget(FName InOldName, FName InNewName)
{
	FName MorphTarget = EditingMeshComponent->RenameMorphTarget(InOldName, InNewName);

	if (float* OverrideWeight = MorphTargetOverrides.Find(InOldName))
	{
		float SavedWeight = *OverrideWeight;
		ClearMorphTargetOverride(InOldName);
		OverrideMorphTargetWeight(InNewName, SavedWeight);
	}

	return MorphTarget;
}

void USkeletalMeshEditingCache::RemoveMorphTargets(const TArray<FName>& InNames)
{
	EditingMeshComponent->RemoveMorphTargets(InNames);

	for (const FName& Name : InNames)
	{
		ClearMorphTargetOverride(Name);
	}
}

TArray<FName> USkeletalMeshEditingCache::DuplicateMorphTargets(const TArray<FName>& InNames)
{
	return EditingMeshComponent->DuplicateMorphTargets(InNames);
}

URefSkeletonPoser* USkeletalMeshEditingCache::GetSkeletonPoser() const
{
	return RefSkeletonPoser;
}


const TArray<FTransform>& USkeletalMeshEditingCache::GetComponentSpaceBoneTransformsRefPose() const
{
	return EditingMeshComponent->GetComponentSpaceBoneTransformsRefPose();
}

const TMap<FName, float>& USkeletalMeshEditingCache::GetSkeletalMeshComponentMorphTargetWeights() const
{
	UAnimInstance* AnimInstance = SkeletalMeshComponent->GetAnimInstance();
	return AnimInstance->GetAnimationCurveList(EAnimCurveType::MorphTargetCurve);
}

void USkeletalMeshEditingCache::DeformPreviewMesh(const TArray<FTransform>& ComponentSpaceTransforms, const TMap<FName, float>& MorphTargetWeights)
{
	if (!PreviewMesh->IsVisible())
	{
		return;
	}

	if (ComponentSpaceTransforms.IsEmpty())
	{
		return;
	}

	if (GetComponentSpaceBoneTransformsRefPose().Num() != ComponentSpaceTransforms.Num())
	{
		return;
	}
	
	using namespace UE::Geometry;
	using namespace SkeletalMeshToolsHelper;
	
	TArray<FMatrix> BoneMatrices = ComputeBoneMatrices(GetComponentSpaceBoneTransformsRefPose(), ComponentSpaceTransforms);
			
	FDynamicMesh3& EditingMesh = *EditingMeshComponent->GetMesh();
				
	auto DeformPreviewMeshFunc = [&](FDynamicMesh3& VisualMesh)
		{
			auto WriteFunc = [&](int32 VertID, const FVector& PosedVertPos)
				{
					VisualMesh.SetVertex(VertID, PosedVertPos);
				};
					
			GetPosedMesh(WriteFunc, EditingMesh, BoneMatrices, NAME_None, MorphTargetWeights);
			FMeshNormals::QuickRecomputeOverlayNormals(VisualMesh);
		};
		
	constexpr bool bRebuildSpatial = false;
	PreviewMesh->DeferredEditMesh(DeformPreviewMeshFunc, bRebuildSpatial);
	PreviewMesh->NotifyDeferredEditCompleted(UPreviewMesh::ERenderUpdateMode::FastUpdate, EMeshRenderAttributeFlags::Positions | EMeshRenderAttributeFlags::VertexNormals, bRebuildSpatial);	
}

void USkeletalMeshEditingCache::UpdateSelectedBoneIndices()
{
	SelectedBoneIndices.Reset();
	for (const FName Name: SelectedBoneNames)
	{
		SelectedBoneIndices.Add(GetEditingMeshComponent()->GetRefSkeleton().FindRawBoneIndex(Name));
	}
}

void USkeletalMeshEditingCache::SetSkeletalMeshSkeletonDrawMode(ESkeletonDrawMode DrawMode) const
{
	if (UDebugSkelMeshComponent* DebugSkelMeshComponent = GetDebugSkelMeshComponent())
	{
		DebugSkelMeshComponent->SkeletonDrawMode = DrawMode;
	}
}

ESkeletonDrawMode USkeletalMeshEditingCache::GetCurrentSkeletonDrawMode() const
{
	return CacheSkeletonDrawMode;
}

FLinearColor USkeletalMeshEditingCache::GetDefaultBoneColor(int32 InBoneIndex) const
{
	// this returns the normal unmodified color of the bone, calling code must account
	// for any editor specific states that might affect the final bone color (like selection)
	
	// skeleton greyed out
	if (GetCurrentSkeletonDrawMode() == ESkeletonDrawMode::GreyedOut)
	{
		return GetDefault<UPersonaOptions>()->DisabledBoneColor;
	}

	// using default color for all bones
	if (!GetDefault<UPersonaOptions>()->bShowBoneColors)
	{
		return GetDefault<UPersonaOptions>()->DefaultBoneColor;
	}
	
	// uses deterministic, semi-random desaturated color unique to the bone index
	return SkeletalDebugRendering::GetSemiRandomColorForBone(InBoneIndex);	
}

UDebugSkelMeshComponent* USkeletalMeshEditingCache::GetDebugSkelMeshComponent() const
{
	return Cast<UDebugSkelMeshComponent>(SkeletalMeshComponent);
}


#undef LOCTEXT_NAMESPACE

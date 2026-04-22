// Copyright Epic Games, Inc. All Rights Reserved.

#include "MirroringTrait.h"

#include "GenerationTools.h"
#include "EvaluationVM/EvaluationVM.h"
#include "TransformArrayOperations.h"
#include "Animation/MirrorDataTable.h"
#include "LODPose.h"
#include "Animation/Skeleton.h"
#include "Engine/SkeletalMesh.h"
#include "AnimationRuntime.h"
#include "Mirroring.h"
#include "BoneContainer.h"

namespace UE::UAF
{
	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// FMirroringTraitCache

	bool FMirroringTraitCache::AreMirrorMapsValid(const UE::UAF::FReferencePose& InReferencePose, const TWeakObjectPtr<const UMirrorDataTable>& InMirrorTable, bool bInShouldMirrorBones, bool bInShouldMirrorAttributes) const
	{
		// Ensure assets are the same.
		
		if (InMirrorTable != MirrorTable)
		{
			return false;
		}

		if (InReferencePose.SkeletalMesh != SkeletalMesh)
		{
			return false;
		}

		// Ensure arrays match mesh bone count.
		
		const int32 ExpectedNumBones = UE::UAF::GetNumOfBonesForMirrorData(InReferencePose);

		// Default to true if we are not mirroring bones since these arrays are only used for that channel.
		const bool bDoesMeshMirrorMapHaveExpectedNumBones = (ExpectedNumBones == MeshBoneIndexToMirroredMeshBoneIndexMap.Num() || !bInShouldMirrorBones);

		// Default to true if we are not mirroring attributes since these arrays are only used for that channel.
		const bool bDoesCompactPoseMirrorMapHaveExpectedNumBones = (ExpectedNumBones == CompactPoseBoneIndexToMirroredCompactPoseBoneIndexMap.Num() || !bInShouldMirrorAttributes);
		
		const bool bAreMirrorMapsEquallySized = bDoesMeshMirrorMapHaveExpectedNumBones && bDoesCompactPoseMirrorMapHaveExpectedNumBones;
		if (!bAreMirrorMapsEquallySized)
		{
			return false;
		}

		// Default to true if we are not mirroring bones since these arrays are only used for that channel.
		const bool bIsRefPoseDataEquallySized = (ExpectedNumBones == MeshSpaceReferencePoseRotations.Num() && ExpectedNumBones == MeshSpaceReferenceRotationCorrections.Num()) || !bInShouldMirrorBones;
		if (!bIsRefPoseDataEquallySized)
		{
			return false;
		}
		
		return true;
	}
	
	bool FMirroringTraitCache::IsReferencePoseDataValid(const UE::UAF::FReferencePose& InReferencePose, bool bInShouldMirrorBones) const
	{
		// Ensure skeletal mesh is still the same.
		if (InReferencePose.SkeletalMesh != SkeletalMesh)
		{
			return false;
		}

		// Ensure arrays match mesh bone count.
		// Default to true if we are not mirroring bones since these arrays are only used for that channel.
		const int32 ExpectedNumBones = UE::UAF::GetNumOfBonesForMirrorData(InReferencePose);
		const bool bIsRefPoseDataEquallySized = (ExpectedNumBones == MeshSpaceReferencePoseRotations.Num() && ExpectedNumBones == MeshSpaceReferenceRotationCorrections.Num()) || !bInShouldMirrorBones;
		if (!bIsRefPoseDataEquallySized)
		{
			return false;
		}
		
		return true;
	}

	bool FMirroringTraitCache::IsValid(const UE::UAF::FReferencePose& InReferencePose, const TWeakObjectPtr<const UMirrorDataTable>& InMirrorTable, bool bInShouldMirrorBones, bool bInShouldMirrorAttributes) const
	{
		return AreMirrorMapsValid(InReferencePose, InMirrorTable, bInShouldMirrorBones, bInShouldMirrorAttributes) && IsReferencePoseDataValid(InReferencePose, bInShouldMirrorBones);
	}

	void FMirroringTraitCache::Clear()
	{
		MeshBoneIndexToMirroredMeshBoneIndexMap.Empty();
		CompactPoseBoneIndexToMirroredCompactPoseBoneIndexMap.Empty();
		MeshSpaceReferencePoseRotations.Empty();
		MeshSpaceReferenceRotationCorrections.Empty();
		MirrorTable.Reset();
		SkeletalMesh.Reset();
	}

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// FMirroringTrait
	
	AUTO_REGISTER_ANIM_TRAIT(FMirroringTrait)
	
	// Trait implementation boilerplate
	#define TRAIT_INTERFACE_ENUMERATOR(GeneratorMacro) \
	GeneratorMacro(IEvaluate) \
	GeneratorMacro(IHierarchy) \
	GeneratorMacro(IGarbageCollection) \
	
	GENERATE_ANIM_TRAIT_IMPLEMENTATION(FMirroringTrait, TRAIT_INTERFACE_ENUMERATOR, NULL_ANIM_TRAIT_INTERFACE_ENUMERATOR, NULL_ANIM_TRAIT_EVENT_ENUMERATOR)
	#undef TRAIT_INTERFACE_ENUMERATOR

	void FMirroringTrait::FInstanceData::Construct(const FExecutionContext& Context, const FTraitBinding& Binding)
	{
		FTrait::FInstanceData::Construct(Context, Binding);
		
		const FSharedData* SharedData = Binding.GetSharedData<FSharedData>();
		FInstanceData* InstanceData = Binding.GetInstanceData<FInstanceData>();
		
		if (!InstanceData->Input.IsValid())
		{
			InstanceData->Input = Context.AllocateNodeInstance(Binding, SharedData->Input);
		}
		
		IGarbageCollection::RegisterWithGC(Context, Binding);
	}

	void FMirroringTrait::FInstanceData::Deconstruct(const FExecutionContext& Context, const FTraitBinding& Binding)
	{
		FTrait::FInstanceData::Destruct(Context, Binding);

		IGarbageCollection::UnregisterWithGC(Context, Binding);
	}

	void FMirroringTrait::PostEvaluate(FEvaluateTraversalContext& Context, const TTraitBinding<IEvaluate>& Binding) const
	{
		IEvaluate::PostEvaluate(Context, Binding);
		
		const FSharedData* SharedData = Binding.GetSharedData<FSharedData>();
		FInstanceData* InstanceData = Binding.GetInstanceData<FInstanceData>();

		// Copy since the settings properties are 'latent' and can change..
		const FMirroringTraitSetupParams LatentSetupValue = SharedData->GetSetup(Binding);;

		InstanceData->bHasMirrorStateChanged = LatentSetupValue.bShouldMirror != InstanceData->Setup.bShouldMirror;
		InstanceData->Setup = LatentSetupValue;
		InstanceData->ApplyTo = SharedData->GetApplyTo(Binding);

		if (!InstanceData->Setup.bShouldMirror || !InstanceData->Input.IsValid() || !InstanceData->Setup.MirrorDataTable)
		{
			return;
		}
		
		Context.AppendTask(FAnimNextEvaluationMirroringTask::Make(InstanceData->Setup, InstanceData->ApplyTo, &InstanceData->Cache));
	}

	uint32 FMirroringTrait::GetNumChildren(const FExecutionContext& Context, const TTraitBinding<IHierarchy>& Binding) const
	{
		return 1;
	}

	void FMirroringTrait::GetChildren(const FExecutionContext& Context, const TTraitBinding<IHierarchy>& Binding, FChildrenArray& Children) const
	{
		const FInstanceData* InstanceData = Binding.GetInstanceData<FInstanceData>();

		// Add the child, even if the handle is empty
		Children.Add(InstanceData->Input);
	}

	void FMirroringTrait::AddReferencedObjects(const FExecutionContext& Context, const TTraitBinding<IGarbageCollection>& Binding, FReferenceCollector& Collector) const
	{
		IGarbageCollection::AddReferencedObjects(Context, Binding, Collector);

		FInstanceData* InstanceData = Binding.GetInstanceData<FInstanceData>();

		Collector.AddReferencedObject(InstanceData->Setup.MirrorDataTable);
	}

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// FMirroringAdditiveTrait
	
	AUTO_REGISTER_ANIM_TRAIT(FMirroringAdditiveTrait)
	
	// Trait implementation boilerplate
	#define TRAIT_INTERFACE_ENUMERATOR(GeneratorMacro) \
	GeneratorMacro(IEvaluate) \
	GeneratorMacro(IGarbageCollection) \
	
	GENERATE_ANIM_TRAIT_IMPLEMENTATION(FMirroringAdditiveTrait, TRAIT_INTERFACE_ENUMERATOR, NULL_ANIM_TRAIT_INTERFACE_ENUMERATOR, NULL_ANIM_TRAIT_EVENT_ENUMERATOR)
	#undef TRAIT_INTERFACE_ENUMERATOR

	void FMirroringAdditiveTrait::FInstanceData::Construct(const FExecutionContext& Context, const FTraitBinding& Binding)
	{
		FTrait::FInstanceData::Construct(Context, Binding);

		IGarbageCollection::RegisterWithGC(Context, Binding);
	}

	void FMirroringAdditiveTrait::FInstanceData::Deconstruct(const FExecutionContext& Context, const FTraitBinding& Binding)
	{
		FTrait::FInstanceData::Destruct(Context, Binding);

		IGarbageCollection::UnregisterWithGC(Context, Binding);
	}

	void FMirroringAdditiveTrait::PostEvaluate(FEvaluateTraversalContext& Context, const TTraitBinding<IEvaluate>& Binding) const
	{
		IEvaluate::PostEvaluate(Context, Binding);
		
		const FSharedData* SharedData = Binding.GetSharedData<FSharedData>();
		FInstanceData* InstanceData = Binding.GetInstanceData<FInstanceData>();

		// Copy since the settings properties are 'latent' and can change.
		const FMirroringTraitSetupParams LatentSetupValue = SharedData->GetSetup(Binding);;

		InstanceData->bHasMirrorStateChanged = LatentSetupValue.bShouldMirror != InstanceData->Setup.bShouldMirror;
		InstanceData->Setup = LatentSetupValue;
		InstanceData->ApplyTo = SharedData->GetApplyTo(Binding);
		
		if (!InstanceData->Setup.bShouldMirror || !InstanceData->Setup.MirrorDataTable)
		{
			return;
		}
		
		Context.AppendTask(FAnimNextEvaluationMirroringTask::Make(InstanceData->Setup, InstanceData->ApplyTo, &InstanceData->Cache));
	}

	void FMirroringAdditiveTrait::AddReferencedObjects(const FExecutionContext& Context, const TTraitBinding<IGarbageCollection>& Binding, FReferenceCollector& Collector) const
	{
		IGarbageCollection::AddReferencedObjects(Context, Binding, Collector);

		FInstanceData* InstanceData = Binding.GetInstanceData<FInstanceData>();

		Collector.AddReferencedObject(InstanceData->Setup.MirrorDataTable);
	}
	
} // namespace UE::UAF

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// FAnimNextEvaluationMirroringTask

FAnimNextEvaluationMirroringTask FAnimNextEvaluationMirroringTask::Make(const UE::UAF::FMirroringTraitSetupParams& Setup, const UE::UAF::FMirroringTraitApplyToParams& ApplyTo, UE::UAF::FMirroringTraitCache* Cache)
{
	FAnimNextEvaluationMirroringTask Task;
	Task.Setup = Setup;
	Task.ApplyTo = ApplyTo;
	Task.Cache = Cache;
	return Task;
}

void FAnimNextEvaluationMirroringTask::Execute(UE::UAF::FEvaluationVM& VM) const
{
	QUICK_SCOPE_CYCLE_COUNTER(FAnimNextEvaluationMirroringTask_Execute);

	// Pop our top pose and start processing it.
	TUniquePtr<UE::UAF::FKeyframeState> Keyframe;
	if (!VM.PopValue(UE::UAF::KEYFRAME_STACK_NAME, Keyframe))
	{
		// We have no inputs, nothing to do.
		return;
	}

	// We don't support mirroring additive pose at the moment.
	if (Keyframe->Pose.IsAdditive())
	{
		UE_LOG(LogAnimation, Warning, TEXT("FAnimNextEvaluationMirroringTask::Execute - Mirroring an additive pose is not supported."))
		
		// Force a bind pose to make it obvious.
		Keyframe->Pose.SetIdentity(true);
		VM.PushValue(UE::UAF::KEYFRAME_STACK_NAME, MoveTemp(Keyframe));
		return;
	}

	// Rebuild mirror cache (if needed)
	UE::UAF::FReferencePose ReferencePose = Keyframe->Pose.GetRefPose();
	EnsureCache(VM, ReferencePose);

	// Determine what channel to skip during this pass.
	bool bShouldMirrorBones = ApplyTo.bShouldMirrorBones && EnumHasAnyFlags(VM.GetFlags(), UE::UAF::EEvaluationFlags::Bones);
	bool bShouldMirrorCurves= ApplyTo.bShouldMirrorCurves && EnumHasAnyFlags(VM.GetFlags(), UE::UAF::EEvaluationFlags::Curves);
	bool bShouldMirrorAttributes = ApplyTo.bShouldMirrorAttributes && EnumHasAnyFlags(VM.GetFlags(), UE::UAF::EEvaluationFlags::Attributes);
	
	// After attempting to rebuild cache if its still invalid something went wrong.
	if (!Cache->IsValid(ReferencePose, Setup.MirrorDataTable, bShouldMirrorBones, bShouldMirrorAttributes))
	{
		// Force a bind pose to make it obvious.
		Keyframe->Pose.SetIdentity(Keyframe->Pose.IsAdditive());
		VM.PushValue(UE::UAF::KEYFRAME_STACK_NAME, MoveTemp(Keyframe));
		return;
	}

	// Mirror keyframe data.
	
	if (bShouldMirrorBones)
	{
		UE::UAF::MirrorPose(
			Keyframe->Pose,
			Setup.MirrorDataTable->MirrorAxis,
			Cache->MeshBoneIndexToMirroredMeshBoneIndexMap,
			Cache->MeshSpaceReferencePoseRotations,
			Cache->MeshSpaceReferenceRotationCorrections);
	}

	if (bShouldMirrorCurves)
	{
		FAnimationRuntime::MirrorCurves(Keyframe->Curves, *Setup.MirrorDataTable);
	}

	if (bShouldMirrorAttributes)
	{
		// @todo: Make UAF version of MirrorAttributes() or address internal heap allocations for scratch array.
		UE::Anim::Attributes::MirrorAttributes(
			Keyframe->Attributes,
			*Setup.MirrorDataTable,
			Cache->CompactPoseBoneIndexToMirroredCompactPoseBoneIndexMap);
	}

	VM.PushValue(UE::UAF::KEYFRAME_STACK_NAME, MoveTemp(Keyframe));
}

void FAnimNextEvaluationMirroringTask::EnsureCache(const UE::UAF::FEvaluationVM& VM, const UE::UAF::FReferencePose& InReferencePose) const
{
	checkf(Cache != nullptr, TEXT("FAnimNextEvaluationMirroringTask::EnsureCache - Invalid cache ptr."))
	
	if (InReferencePose.SkeletalMesh.IsValid())
	{
		const int32 NumBonesForLOD0 = UE::UAF::GetNumOfBonesForMirrorData(InReferencePose);
		const bool bShouldMirrorBones = ApplyTo.bShouldMirrorBones && EnumHasAnyFlags(VM.GetFlags(), UE::UAF::EEvaluationFlags::Bones);
		const bool bShouldMirrorAttributes = ApplyTo.bShouldMirrorAttributes && EnumHasAnyFlags(VM.GetFlags(), UE::UAF::EEvaluationFlags::Attributes);
		const bool bAreMirrorMapsValid = Cache->AreMirrorMapsValid(InReferencePose, Setup.MirrorDataTable, bShouldMirrorBones, bShouldMirrorAttributes);
		const bool bIsReferencePoseDataValid = Cache->IsReferencePoseDataValid(InReferencePose, bShouldMirrorBones);
		
		if (bShouldMirrorBones)
		{
			if (!bAreMirrorMapsValid)
			{
				Cache->MeshBoneIndexToMirroredMeshBoneIndexMap.SetNumUninitialized(NumBonesForLOD0);
				
				UE::UAF::BuildMeshBoneIndexMirrorMap(InReferencePose, *Setup.MirrorDataTable, Cache->MeshBoneIndexToMirroredMeshBoneIndexMap);
			}
			
			if (!bIsReferencePoseDataValid)
			{
				Cache->MeshSpaceReferencePoseRotations.SetNumUninitialized(NumBonesForLOD0);
				Cache->MeshSpaceReferenceRotationCorrections.SetNumUninitialized(NumBonesForLOD0);
				
				UE::UAF::BuildReferencePoseMirrorData(
					InReferencePose,
					Setup.MirrorDataTable->MirrorAxis,
					Cache->MeshBoneIndexToMirroredMeshBoneIndexMap,
					Cache->MeshSpaceReferencePoseRotations,
					Cache->MeshSpaceReferenceRotationCorrections
				);
			}
		}
		
		if (bShouldMirrorAttributes)
		{
			if (!bAreMirrorMapsValid)
			{
				// We only care about the mesh bones (LOD0).
				TArray<FBoneIndexType> RequiredBoneIndexArray;
				RequiredBoneIndexArray.AddUninitialized(NumBonesForLOD0);
				
				for (int32 BoneIndex = 0; BoneIndex < RequiredBoneIndexArray.Num(); ++BoneIndex)
				{
					RequiredBoneIndexArray[BoneIndex] = InReferencePose.GetSkeletonBoneIndexFromLODBoneIndex(BoneIndex);
				}

				// Compute mirror map for compact pose.
				
				FBoneContainer BoneContainer;
				BoneContainer.InitializeTo(RequiredBoneIndexArray, UE::Anim::FCurveFilterSettings(UE::Anim::ECurveFilterMode::DisallowAll), *InReferencePose.SkeletalMesh);
				
				TCustomBoneIndexArray<FSkeletonPoseBoneIndex, FSkeletonPoseBoneIndex> MirrorBoneIndexes;
				Setup.MirrorDataTable->FillMirrorBoneIndexes(BoneContainer.GetSkeletonAsset(), MirrorBoneIndexes);
				Setup.MirrorDataTable->FillCompactPoseMirrorBones(BoneContainer, MirrorBoneIndexes, Cache->CompactPoseBoneIndexToMirroredCompactPoseBoneIndexMap);
			}
		}
	}
	else
	{
		// Clear data to terminate task early.
		Cache->Clear();
		
		UE_LOG(LogAnimation, Warning, TEXT("FAnimNextEvaluationMirroringTask::EnsureCache - Failed to get skeletal mesh asset from reference pose."))
	}

	// Keep track of latest assets used to build the cache
	Cache->SkeletalMesh = InReferencePose.SkeletalMesh;
	Cache->MirrorTable = Setup.MirrorDataTable;
}
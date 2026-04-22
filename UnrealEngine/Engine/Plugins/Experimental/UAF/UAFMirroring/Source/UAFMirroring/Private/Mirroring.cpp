// Copyright Epic Games, Inc. All Rights Reserved.

#include "Mirroring.h"

#include "EvaluationVM/EvaluationVM.h"
#include "TransformArrayOperations.h"
#include "Animation/MirrorDataTable.h"
#include "LODPose.h"
#include "AnimationRuntime.h"
#include "Engine/DataTable.h"
#include "Logging/StructuredLog.h"
#include "Engine/SkeletalMesh.h"

namespace UE::UAF
{
	using FMemStackSetAllocator = TSetAllocator<TSparseArrayAllocator<FAnimStackAllocator, FAnimStackAllocator>, FAnimStackAllocator>;

	int32 GetNumOfBonesForMirrorData(const FReferencePose& InReferencePose)
	{
		return InReferencePose.GetNumBonesForLOD(0);
	}
	
	void BuildReferencePoseMirrorData(
		const FReferencePose& InReferencePose,
		EAxis::Type InMirrorAxis,
		TConstArrayView<FBoneIndexType> InMeshBoneIndexToMirroredMeshBoneIndexMap,
		TArrayView<FQuat> OutRefPoseMeshSpaceRotations,
		TArrayView<FQuat> OutRefPoseMeshSpaceRotationCorrections)
	{
		const int32 RefPoseBoneNum = InReferencePose.ReferenceLocalTransforms.Num();
		
		const TArrayView<const FBoneIndexType>& MeshBoneIndexToParentMeshBoneIndexMap = InReferencePose.MeshBoneIndexToParentMeshBoneIndexMap;

		// All the reference arrays are sized of LOD0 since we can always truncate the arrays if we need higher LOD levels. 
		checkf(OutRefPoseMeshSpaceRotations.Num() == RefPoseBoneNum, TEXT("Buffer mismatch: %d:%d"), OutRefPoseMeshSpaceRotations.Num() , RefPoseBoneNum);
		checkf(OutRefPoseMeshSpaceRotationCorrections.Num() == RefPoseBoneNum, TEXT("Buffer mismatch: %d:%d"), OutRefPoseMeshSpaceRotationCorrections.Num(), RefPoseBoneNum);
		checkf(InMeshBoneIndexToMirroredMeshBoneIndexMap.Num() == RefPoseBoneNum, TEXT("Buffer mismatch: %d:%d"), InMeshBoneIndexToMirroredMeshBoneIndexMap.Num(), RefPoseBoneNum)
		checkf(MeshBoneIndexToParentMeshBoneIndexMap.Num() == RefPoseBoneNum, TEXT("Buffer mismatch: %d:%d"), MeshBoneIndexToParentMeshBoneIndexMap.Num(), RefPoseBoneNum);
		
		// Copy local ref pose rotations.
		FMemory::Memcpy(OutRefPoseMeshSpaceRotations.GetData(), InReferencePose.ReferenceLocalTransforms.Rotations.GetData(), RefPoseBoneNum * sizeof(FQuat));

		// Convert the local ref pose rotations to component space.
		// @note: We assume the root bone is at index 0.
		for (int32 RefBoneIndex = 1; RefBoneIndex < RefPoseBoneNum; ++RefBoneIndex)
		{
			const FBoneIndexType ParentBoneIndex = MeshBoneIndexToParentMeshBoneIndexMap[RefBoneIndex];
			
			const FQuat ParentLocalSpaceRotation = OutRefPoseMeshSpaceRotations[ParentBoneIndex];
			const FQuat RefBoneLocalSpaceRotation = OutRefPoseMeshSpaceRotations[RefBoneIndex];
			
			FQuat MeshSpaceRotation =  ParentLocalSpaceRotation * RefBoneLocalSpaceRotation;
			MeshSpaceRotation.Normalize();
			
			OutRefPoseMeshSpaceRotations[RefBoneIndex] = MeshSpaceRotation;
		}

		// Now we can precompute the corrective rotation to align result with target space's rest orientation.
		OutRefPoseMeshSpaceRotationCorrections[0] = FQuat::Identity;
		for (FBoneIndexType RefBoneIndex = 1; RefBoneIndex < RefPoseBoneNum; ++RefBoneIndex)
		{
			const FBoneIndexType SourceBoneIndex = RefBoneIndex;
			const FBoneIndexType TargetBoneIndex = InMeshBoneIndexToMirroredMeshBoneIndexMap[SourceBoneIndex];

			// Not mapped, so skip.
			if (TargetBoneIndex == FBoneIndexType(INDEX_NONE))
			{
				continue;
			}
			
			OutRefPoseMeshSpaceRotationCorrections[RefBoneIndex] = FAnimationRuntime::MirrorQuat(OutRefPoseMeshSpaceRotations[SourceBoneIndex], InMirrorAxis).Inverse() * OutRefPoseMeshSpaceRotations[TargetBoneIndex];
			OutRefPoseMeshSpaceRotationCorrections[RefBoneIndex].Normalize();
		}
	}
	
	// Get a map from source bone index to mirrored bone index.
	void BuildMeshBoneIndexMirrorMap(
		const FReferencePose& InReferencePose,
		const UMirrorDataTable& InMirrorDataTable,
		TArrayView<FBoneIndexType> OutMeshBoneIndexToMirroredMeshBoneIndexMap)
	{
		// Add this in case this code is called outside update/eval pass in UAF.
		FMemMark Mark(FMemStack::Get());
		
		const int32 RefPoseBoneNum = InReferencePose.ReferenceLocalTransforms.Num();
		
		// All the reference arrays are sized of LOD0 since we can always truncate the arrays if we need higher LOD levels. 
		checkf(RefPoseBoneNum == OutMeshBoneIndexToMirroredMeshBoneIndexMap.Num(), TEXT("Buffer mismatch: %d:%d"), RefPoseBoneNum, OutMeshBoneIndexToMirroredMeshBoneIndexMap.Num());
		
		// Reset the mirror table to defaults (no mirroring).
		FMemory::Memset(OutMeshBoneIndexToMirroredMeshBoneIndexMap.GetData(), INDEX_NONE, RefPoseBoneNum * OutMeshBoneIndexToMirroredMeshBoneIndexMap.GetTypeSize());

		// Query only bone info from mirror data table.
		TMap<FName, FName, TInlineSetAllocator<128, FMemStackSetAllocator>> NameToMirrorNameBoneMap;
		InMirrorDataTable.ForeachRow<FMirrorTableRow>(TEXT("UE::UAF::FillMirrorBoneIndexes"), [&NameToMirrorNameBoneMap](const FName& Key, const FMirrorTableRow& Value) mutable
			{
				if (Value.MirrorEntryType == EMirrorRowType::Bone)
				{
					NameToMirrorNameBoneMap.Add(Value.Name, Value.MirroredName);
				}
			}
		);

		// We need the reference skeleton to be able to query bone names.
		// @todo: Will this change once we have abstract hierarchies?
		const USkeletalMesh* SkeletalMesh = InReferencePose.GetSkeletalMeshAsset();
		checkf(SkeletalMesh != nullptr, TEXT("UAF::FillMirrorBoneIndices - SkeletalMesh is null"));
		const FReferenceSkeleton& ReferenceSkeleton = SkeletalMesh->GetRefSkeleton();
		
		const TArrayView<const FBoneIndexType> MeshBoneIndexMapToLODBoneIndex = InReferencePose.GetMeshBoneIndexToLODBoneIndexMap();
		checkf(MeshBoneIndexMapToLODBoneIndex.Num() == RefPoseBoneNum, TEXT("Buffer mismatch: %d:%d"), MeshBoneIndexMapToLODBoneIndex.Num(), RefPoseBoneNum);

		// Build mirror map for all the mesh bones in reference pose (LOD0).
		for (FBoneIndexType MeshBoneIndex = 0; MeshBoneIndex < OutMeshBoneIndexToMirroredMeshBoneIndexMap.Num(); ++MeshBoneIndex)
		{
			if (OutMeshBoneIndexToMirroredMeshBoneIndexMap[MeshBoneIndex] == FBoneIndexType(INDEX_NONE))
			{
				// Find the candidate mirror partner for this bone (falling back to mirroring to self)
				const FName SourceBoneName = ReferenceSkeleton.GetBoneName(MeshBoneIndex);
				int32 MirrorMeshBoneIndex = INDEX_NONE;
				
				FName* MirroredBoneName = NameToMirrorNameBoneMap.Find(SourceBoneName);
				if (!SourceBoneName.IsNone() && MirroredBoneName)
				{
					MirrorMeshBoneIndex = ReferenceSkeleton.FindBoneIndex(*MirroredBoneName);
				}
				
				OutMeshBoneIndexToMirroredMeshBoneIndexMap[MeshBoneIndex] = MirrorMeshBoneIndex;
				
				// Map candidate mirror partner to current bone.
				// @todo: What happens we have conflicts on mirror data table setup. Should we have a conflict policy to resolve this? For example, two bones map to another.
				if (MirrorMeshBoneIndex != INDEX_NONE)
				{
					OutMeshBoneIndexToMirroredMeshBoneIndexMap[MirrorMeshBoneIndex] = MeshBoneIndex;
				}
			}
		}
	}

	void MirrorPose(
		FLODPose& InOutLODPose,
		const UMirrorDataTable& InMirrorDataTable)
	{
		const int32 SourceLODBoneNum = InOutLODPose.LocalTransformsView.Num();
		
		if (InMirrorDataTable.MirrorAxis == EAxis::None)
		{
			UE_LOG(LogAnimation, Warning, TEXT("UAF::MirrorPose - No mirror axis provided."));
			return;
		}

		if (SourceLODBoneNum == 0)
		{
			UE_LOG(LogAnimation, Warning, TEXT("UAF::MirrorPose - Attempting to mirror an empty pose."));
			return;
		}

		if (InOutLODPose.IsAdditive())
		{
			UE_LOG(LogAnimation, Warning, TEXT("UAF::MirrorPose - Attempting to mirror an additive pose."));
			return;
		}
		
		// Get mirrored bones mapping.
		TArray<FBoneIndexType, FAnimStackAllocator> MeshBoneIndexToMirroredMeshBoneIndexMap;
		TArray<FQuat, FAnimStackAllocator> ComponentSpaceReferencePoseRotations;
		TArray<FQuat, FAnimStackAllocator> ComponentSpaceReferenceRotationCorrections;

		const FReferencePose& ReferencePose = InOutLODPose.GetRefPose();
		
		const int32 MirrorBoneIndicesNum = GetNumOfBonesForMirrorData(ReferencePose);
		MeshBoneIndexToMirroredMeshBoneIndexMap.SetNumUninitialized(MirrorBoneIndicesNum);

		const int32 BindPoseMirrorDataNum = GetNumOfBonesForMirrorData(InOutLODPose.GetRefPose());
		ComponentSpaceReferencePoseRotations.SetNumUninitialized(BindPoseMirrorDataNum);
		ComponentSpaceReferenceRotationCorrections.SetNumUninitialized(BindPoseMirrorDataNum);
		
		BuildMeshBoneIndexMirrorMap(ReferencePose, InMirrorDataTable, MeshBoneIndexToMirroredMeshBoneIndexMap);
		BuildReferencePoseMirrorData(ReferencePose, InMirrorDataTable.MirrorAxis, MeshBoneIndexToMirroredMeshBoneIndexMap, ComponentSpaceReferencePoseRotations, ComponentSpaceReferenceRotationCorrections);
		
		MirrorPose(InOutLODPose, InMirrorDataTable.MirrorAxis, MeshBoneIndexToMirroredMeshBoneIndexMap, ComponentSpaceReferencePoseRotations, ComponentSpaceReferenceRotationCorrections);
	}
	
	void MirrorPose(
		FLODPose& InOutLODPose,
		EAxis::Type InMirrorAxis,
		TConstArrayView<FBoneIndexType> InMeshBoneIndexToMirroredMeshBoneIndexMap,
		TConstArrayView<FQuat> InRefPoseMeshSpaceRotations,
		TConstArrayView<FQuat> InRefPoseMeshSpaceRotationCorrections)
	{
		// @todo: check mirror map size
		
		const int32 SourceLODBoneNum = InOutLODPose.LocalTransformsView.Num();
		
		const int32 RefPoseBoneNum = InOutLODPose.GetRefPose().ReferenceLocalTransforms.Num();
		checkf(RefPoseBoneNum == InMeshBoneIndexToMirroredMeshBoneIndexMap.Num(), TEXT("Buffer mismatch: %d:%d"), RefPoseBoneNum, InMeshBoneIndexToMirroredMeshBoneIndexMap.Num());
		
		if (InMirrorAxis == EAxis::None )
		{
			UE_LOG(LogAnimation, Warning, TEXT("UAF::MirrorPose - No mirror axis provided."));
			return;
		}

		if (SourceLODBoneNum == 0)
		{
			UE_LOG(LogAnimation, Warning, TEXT("UAF::MirrorPose - Attempting to mirror an empty pose."));
			return;
		}

		if (InOutLODPose.IsAdditive())
		{
			UE_LOG(LogAnimation, Warning, TEXT("UAF::MirrorPose - Attempting to mirror an additive pose."));
			return;
		}
		
		const TArrayView<const FBoneIndexType>& LODBoneIndexToParentLODBoneIndexMap = InOutLODPose.GetLODBoneIndexToParentLODBoneIndexMap();
		checkf(LODBoneIndexToParentLODBoneIndexMap.Num() == InOutLODPose.GetNumBones(), TEXT("Buffer mismatch: %d:%d"), LODBoneIndexToParentLODBoneIndexMap.Num(), InOutLODPose.GetNumBones());
		
		// Mirroring is authored in object space and as such we must transform the local space transforms in object space in order
		// to apply the object space mirroring axis. To facilitate this, we use object space transforms for the bind pose which can be cached.
		// We ignore the translation/scale part of the bind pose as they don't impact mirroring.
		// 
		// Rotations, translations, and scales are all treated differently:
		//    Rotation:
		//        We transform the local space rotation into object space
		//        We mirror the rotation axis
		//        We apply a correction: if the source and target bones are different, we must account for the mirrored delta between the two
		//        We transform the result back into local space
		//    Translation:
		//        We rotate the local space translation into object space
		//        We mirror the result
		//        We then rotate it back into local space
		//    Scale:
		//        Mirroring does not modify scale
		// 
		// This sadly doesn't quite work for additive poses because in order to transform it into the bind pose reference frame,
		// we need the base pose it is applied on. Worse still, the base pose might not be static, it could be a time scaled sequence.
		//
		// Contract/Assumption: the (SourceBoneIndex -> TargetBoneIndex) mapping used to call this matches the mapping used to build OutMirrorCorrections (i.e., Target == MirrorMap[Source]).
		const auto MirrorTransform = [&InRefPoseMeshSpaceRotations, &InMirrorAxis, InRefPoseMeshSpaceRotationCorrections](const FTransform& SourceTransform, const FBoneIndexType& SourceParentIndex, const FBoneIndexType& SourceBoneIndex, const FBoneIndexType& TargetParentIndex, const FBoneIndexType& TargetBoneIndex) -> FTransform
		{
			const FQuat TargetParentRefRotation = TargetParentIndex != FBoneIndexType(INDEX_NONE) ? InRefPoseMeshSpaceRotations[TargetParentIndex] : FQuat::Identity;
			const FQuat SourceParentRefRotation = SourceParentIndex != FBoneIndexType(INDEX_NONE) ? InRefPoseMeshSpaceRotations[SourceParentIndex] : FQuat::Identity;

			// Mirror the translation component: Rotate the translation into the space of the mirror plane, mirror across the plane, and rotate into the space of its new parent.

			FVector T = SourceTransform.GetTranslation();
			T = SourceParentRefRotation.RotateVector(T);          // to mesh space (component space)
			T = FAnimationRuntime::MirrorVector(T, InMirrorAxis); // reflect across plane
			T = TargetParentRefRotation.UnrotateVector(T);        // back to target's parent local space

			// Mirror the rotation component: Rotate into the space of the mirror plane, mirror across the plane, apply corrective rotation to align result with target space's rest orientation, 
			// then rotate into the space of its new parent

			
			FQuat Q = SourceTransform.GetRotation();
			Q = SourceParentRefRotation * Q;                     // to mesh space (component space)
			Q = FAnimationRuntime::MirrorQuat(Q, InMirrorAxis);  // reflect accros plane

			// Bind alignment correction: mirror the source bone's bind orientation and align to target bone's bind orientation.
			// @note: we use a precomputed correction quat to be able to save a MirrorQuat() since this can be cached once per skeleton,
			// this assumes that source always is the mesh bone index and target always is the mirrored mesh bone index.
			Q *= InRefPoseMeshSpaceRotationCorrections[SourceBoneIndex];
			Q = TargetParentRefRotation.Inverse() * Q;
			Q.Normalize();

			// Scale is not affected by mirroring.
			FVector S = SourceTransform.GetScale3D();

			return FTransform(Q, T, S);
		};
		
		// Mirror the root bone.
		{
			// @todo: Can the root bone be other than 0? LODPose.GetRefPose().RootBoneIndex? Can we have more than one root bone? 
			FBoneIndexType RootBoneIndex = 0;
			FBoneIndexType MirrorRootBoneIndex = InMeshBoneIndexToMirroredMeshBoneIndexMap[RootBoneIndex];

			if (MirrorRootBoneIndex != FBoneIndexType(INDEX_NONE))
			{
				const FBoneIndexType TargetParentIndex = LODBoneIndexToParentLODBoneIndexMap[RootBoneIndex];
				const FBoneIndexType SourceParentIndex = LODBoneIndexToParentLODBoneIndexMap[MirrorRootBoneIndex];

				if (TargetParentIndex != FBoneIndexType(INDEX_NONE))
				{
					UE_LOGFMT(LogAnimation, Error, "UAF::MirrorPose - Found parent bone index ({0}) for root bone index ({1}). This is invalid.", TargetParentIndex, RootBoneIndex);
					return;
				}
				
				if (TargetParentIndex == FBoneIndexType(INDEX_NONE) && SourceParentIndex != FBoneIndexType(INDEX_NONE))
				{
					UE_LOGFMT(LogAnimation, Error, "UAF::MirrorPose - Mapping root bone to non-root bone is not supported ({0} -> {1})", RootBoneIndex, MirrorRootBoneIndex);
					return;
				}
				
				InOutLODPose.LocalTransformsView[RootBoneIndex] = MirrorTransform(InOutLODPose.LocalTransformsView[MirrorRootBoneIndex], SourceParentIndex, MirrorRootBoneIndex, TargetParentIndex, RootBoneIndex);
			}
		}
		
		// Mirror the non-root bones.
		for (FBoneIndexType LODBoneIndex = 1; LODBoneIndex < SourceLODBoneNum; ++LODBoneIndex)
		{
			// @note: We can safely use the LODBoneIndex to get our mirrored name since the map is in LOD0 which will always contain the index.
			const FBoneIndexType MirroredLODBoneIndex = InMeshBoneIndexToMirroredMeshBoneIndexMap[LODBoneIndex];
			const FBoneIndexType ParentLODBoneIndex = LODBoneIndexToParentLODBoneIndexMap[LODBoneIndex];
			
			// Self-mirror (mirror in place relative to the same parent).
			if (LODBoneIndex == MirroredLODBoneIndex)
			{
				InOutLODPose.LocalTransformsView[LODBoneIndex] = MirrorTransform(InOutLODPose.LocalTransformsView[LODBoneIndex], ParentLODBoneIndex, LODBoneIndex, ParentLODBoneIndex, LODBoneIndex);
				continue;
			}

			// Skip invalid or already-processed mirror pairs.
			if (MirroredLODBoneIndex == FBoneIndexType(INDEX_NONE) || MirroredLODBoneIndex < LODBoneIndex)
			{
				continue;
			}
			
			const FBoneIndexType MirroredParentLODIndex = LODBoneIndexToParentLODBoneIndexMap[MirroredLODBoneIndex];

			const FTransform OriginalTransformAtBoneIndex = InOutLODPose.LocalTransformsView[LODBoneIndex];
			const FTransform OriginalTransformAtMirroredIndex = InOutLODPose.LocalTransformsView[MirroredLODBoneIndex];
			
			const FTransform NewTransformAtBoneIndex = MirrorTransform(OriginalTransformAtMirroredIndex, MirroredParentLODIndex, MirroredLODBoneIndex, ParentLODBoneIndex, LODBoneIndex);
			const FTransform NewTransformAtMirroredIndex = MirrorTransform(OriginalTransformAtBoneIndex, ParentLODBoneIndex, LODBoneIndex, MirroredParentLODIndex, MirroredLODBoneIndex);
			
			InOutLODPose.LocalTransformsView[LODBoneIndex] = NewTransformAtBoneIndex;
			InOutLODPose.LocalTransformsView[MirroredLODBoneIndex] = NewTransformAtMirroredIndex;
		}
	}
}

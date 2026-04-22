// Copyright Epic Games, Inc. All Rights Reserved.

#include "SkinningSceneExtensionProxy.h"
#include "SkeletalRenderPublic.h"
#include "SkinningDefinitions.h"
#include "AnimationRuntime.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "SkinnedMeshSceneProxyDesc.h"
#include "InstancedSkinnedMeshSceneProxyDesc.h"

extern TAutoConsoleVariable<int32> CVarInstancedSkinnedMeshesForceRefPose;

FSkinningSceneExtensionProxy::FSkinningSceneExtensionProxy(FSkeletalMeshObject* InMeshObject, const USkinnedAsset* InSkinnedAsset, bool bAllowScaling)
	: SkinnedAsset(InSkinnedAsset)
	, MeshObject(InMeshObject)
{
	FSkeletalMeshRenderData& RenderData = MeshObject->GetSkeletalMeshRenderData();
	MaxBoneInfluenceCount = RenderData.GetNumBoneInfluences();

	if (MeshObject->IsGPUSkinMesh())
	{
		const int32 MostDetailedLODIndex = 0; // TODO: Support LOD switching - needs dynamic data updates.
		TConstArrayView<FSkelMeshRenderSection> Sections = MeshObject->GetRenderSections(MostDetailedLODIndex);

		MaxBoneTransformCount = 0;
		for (const FSkelMeshRenderSection& Section : Sections)
		{
			if (Section.IsValid())
			{
				MaxBoneTransformCount += Section.BoneMap.Num();
			}
		}
		BoneHierarchy.Reserve(MaxBoneTransformCount);

		for (const FSkelMeshRenderSection& Section : Sections)
		{
			if (Section.IsValid())
			{
				for (FBoneIndexType BoneIndex : Section.BoneMap)
				{
					BoneHierarchy.Emplace(BoneIndex);
				}
			}
		}

		bUseSectionBoneMap = true;
	}
	else
	{
		const FReferenceSkeleton& RefSkeleton = SkinnedAsset->GetRefSkeleton();
		const TArray<FTransform>& RefBonePose = RefSkeleton.GetRawRefBonePose();

		TArray<FTransform> ComponentTransforms;
		FAnimationRuntime::FillUpComponentSpaceTransforms(RefSkeleton, RefBonePose, ComponentTransforms);

		const uint16 MaxRawBoneCount = uint16(RefSkeleton.GetRawBoneNum());
		MaxBoneTransformCount = MaxRawBoneCount;

		BoneHierarchy.SetNumUninitialized(MaxRawBoneCount);

		bHasScale = false;

		const bool bRemoveScale = !bAllowScaling;

		for (int32 BoneIndex = 0; BoneIndex < MaxRawBoneCount; ++BoneIndex)
		{
			struct FPackedBone
			{
				uint32 BoneParent : 16;
				uint32 BoneDepth : 16;
			}
			Packed;

			const int32 ParentBoneIndex	= RefSkeleton.GetRawParentIndex(BoneIndex);
			const int32 BoneDepth		= RefSkeleton.GetDepthBetweenBones(BoneIndex, 0);
			Packed.BoneParent			= uint16(ParentBoneIndex);
			Packed.BoneDepth			= uint16(BoneDepth);
			BoneHierarchy[BoneIndex]	= *reinterpret_cast<uint32*>(&Packed);

			if (bRemoveScale)
			{
				ComponentTransforms[BoneIndex].RemoveScaling();
			}
			else if (!bHasScale && !FMath::IsNearlyEqual((float)ComponentTransforms[BoneIndex].GetDeterminant(), 1.0f, UE_KINDA_SMALL_NUMBER))
			{
				bHasScale = true;
			}
		}

		// TODO: Shrink/compress representation further
		// Drop one of the rotation components (largest value) and store index in 4 bits to reconstruct
		// 16b fixed point? Variable rate?
		const uint32 FloatCount = GetObjectSpaceFloatCount();
		BoneObjectSpace.SetNumUninitialized(MaxRawBoneCount * FloatCount);
		float* WritePtr = BoneObjectSpace.GetData();
		for (int32 BoneIndex = 0; BoneIndex < MaxRawBoneCount; ++BoneIndex)
		{
			const FTransform& Transform = ComponentTransforms[BoneIndex];
			const FQuat& Rotation = Transform.GetRotation();
			const FVector& Translation = Transform.GetTranslation();

			WritePtr[0] = (float)Rotation.X;
			WritePtr[1] = (float)Rotation.Y;
			WritePtr[2] = (float)Rotation.Z;
			WritePtr[3] = (float)Rotation.W;

			WritePtr[4] = (float)Translation.X;
			WritePtr[5] = (float)Translation.Y;
			WritePtr[6] = (float)Translation.Z;

			if (bHasScale)
			{
				const FVector& Scale = Transform.GetScale3D();
				WritePtr[7] = (float)Scale.X;
				WritePtr[8] = (float)Scale.Y;
				WritePtr[9] = (float)Scale.Z;
			}

			WritePtr += FloatCount;
		}
	}
}

TConstArrayView<uint64> FSkinningSceneExtensionProxy::GetAnimationProviderData(bool& bOutValid) const
{
	bOutValid = false;
	return {};
}

const FGuid& FSkinningSceneExtensionProxy::GetTransformProviderId() const
{
	static FGuid AnimRuntimeId(ANIM_RUNTIME_TRANSFORM_PROVIDER_GUID);
	return AnimRuntimeId;
}

FInstancedSkinningSceneExtensionProxy::FInstancedSkinningSceneExtensionProxy(TObjectPtr<UTransformProviderData> InTransformProvider, FSkeletalMeshObject* InMeshObject, const USkinnedAsset* InSkinnedAsset, bool bAllowScaling)
	: FSkinningSceneExtensionProxy(InMeshObject, InSkinnedAsset, bAllowScaling)
	, TransformProvider(InTransformProvider)
{
	bUseInstancing = true;

	const bool bForceRefPose = CVarInstancedSkinnedMeshesForceRefPose.GetValueOnAnyThread() != 0;
	if (!bForceRefPose && GetSkinnedAsset() && TransformProvider != nullptr && TransformProvider->IsEnabled())
	{
		TransformProviderId  = TransformProvider->GetTransformProviderID();
		UniqueAnimationCount = TransformProvider->GetUniqueAnimationCount();
		bUseSkeletonBatching = TransformProvider->UsesSkeletonBatching();
	}
	else
	{
		UniqueAnimationCount = 1;
		bUseSkeletonBatching = false;

		static FGuid RefPoseProviderId(REF_POSE_TRANSFORM_PROVIDER_GUID);
		TransformProviderId = RefPoseProviderId;
	}
}

void FInstancedSkinningSceneExtensionProxy::CreateRenderThreadResources(FSceneInterface& Scene, FRHICommandListBase& RHICmdList)
{
	if (TransformProvider != nullptr && TransformProvider->GetTransformProviderID() == TransformProviderId)
	{
		TransformProviderProxy = TransformProvider->CreateRenderThreadResources(this, Scene, RHICmdList);
	}
	else
	{
		TransformProviderProxy = nullptr;
	}
}

void FInstancedSkinningSceneExtensionProxy::DestroyRenderThreadResources()
{
	if (TransformProviderProxy != nullptr)
	{
		TransformProvider->DestroyRenderThreadResources(TransformProviderProxy);
		TransformProviderProxy = nullptr;
	}
}

TConstArrayView<uint64> FInstancedSkinningSceneExtensionProxy::GetAnimationProviderData(bool& bOutValid) const
{
	if (TransformProviderProxy != nullptr)
	{
		return TransformProviderProxy->GetProviderData(bOutValid);
	}

	bOutValid = false;
	return {};
}

const FGuid& FInstancedSkinningSceneExtensionProxy::GetTransformProviderId() const
{
	// If the proxy is current in an invalid state, use the
	// reference pose transform provider
	if (TransformProviderId.IsValid())
	{
		bool bIsValid = false;
		GetAnimationProviderData(bIsValid);
		if (!bIsValid)
		{
			static FGuid RefPoseProviderId(REF_POSE_TRANSFORM_PROVIDER_GUID);
			return RefPoseProviderId;
		}
	}

	return TransformProviderId;
}
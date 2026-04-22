// Copyright Epic Games, Inc. All Rights Reserved.

#include "InstancedSkinnedMeshSceneProxyDesc.h"
#include "Components/InstancedSkinnedMeshComponent.h"
#include "SkeletalRenderNanite.h"
#include "SkeletalRenderGPUSkin.h"
#include "InstancedSkinnedMeshSceneProxy.h"
#include "Rendering/NaniteResourcesHelper.h"

FSkeletalMeshObject* FInstancedSkinnedMeshSceneProxyDesc::CreateMeshObject(const FInstancedSkinnedMeshSceneProxyDesc& InMeshDesc, FSkeletalMeshRenderData* InRenderData, ERHIFeatureLevel::Type InFeatureLevel)
{
	if (InMeshDesc.ShouldNaniteSkin())
	{
		return new FInstancedSkeletalMeshObjectNanite(InMeshDesc, InRenderData, InFeatureLevel);
	}
#if USE_SKINNING_SCENE_EXTENSION_FOR_NON_NANITE
	else if (!InMeshDesc.ShouldCPUSkin())
	{
		return new FInstancedSkeletalMeshObjectGPUSkin(InMeshDesc, InRenderData, InFeatureLevel);
	}
#endif
	return nullptr;
}

FPrimitiveSceneProxy* FInstancedSkinnedMeshSceneProxyDesc::CreateSceneProxy(const FInstancedSkinnedMeshSceneProxyDesc& Desc, bool bHideSkin, bool bShouldNaniteSkin, bool bIsEnabled, int32 MinLODIndex)
{
	LLM_SCOPE(ELLMTag::SkeletalMesh);
	FPrimitiveSceneProxy* Result = nullptr;
	FSkeletalMeshRenderData* SkelMeshRenderData = Desc.GetSkinnedAsset()->GetResourceForRendering();

	FSkeletalMeshObject* MeshObject = Desc.MeshObject;

	// Only create a scene proxy for rendering if properly initialized
	if (SkelMeshRenderData &&
		SkelMeshRenderData->LODRenderData.IsValidIndex(Desc.PredictedLODLevel) &&
		!bHideSkin &&
		MeshObject)
	{
		// Only create a scene proxy if the bone count being used is supported, or if we don't have a skeleton (this is the case with destructibles)
		int32 MaxBonesPerChunk = SkelMeshRenderData->GetMaxBonesPerSection(MinLODIndex);
		int32 MaxSupportedNumBones = MeshObject->IsCPUSkinned() ? MAX_int32 : FGPUBaseSkinVertexFactory::GetMaxGPUSkinBones();
		if (MaxBonesPerChunk <= MaxSupportedNumBones)
		{
			if (bIsEnabled)
			{
				if (bShouldNaniteSkin)
				{
					Nanite::FMaterialAudit NaniteMaterials{};
					const bool bSetMaterialUsageFlags = true;
					Nanite::FNaniteResourcesHelper::AuditMaterials(&Desc, NaniteMaterials, bSetMaterialUsageFlags);

					const bool bForceNaniteForMasked = false;
					const bool bIsMaskingAllowed = Nanite::IsMaskingAllowed(Desc.GetWorld(), bForceNaniteForMasked);
					if (NaniteMaterials.IsValid(bIsMaskingAllowed))
					{
						Result = ::new FNaniteInstancedSkinnedMeshSceneProxy(NaniteMaterials, Desc, SkelMeshRenderData);
					}
				}
#if USE_SKINNING_SCENE_EXTENSION_FOR_NON_NANITE
				else if (MeshObject->IsGPUSkinMesh())
				{
					Result = ::new FInstancedSkinnedMeshSceneProxy(Desc, SkelMeshRenderData);
				}
#endif
			}

			if (Result == nullptr)
			{
				Result = FSkinnedMeshSceneProxyDesc::CreateSceneProxy(Desc, bHideSkin, MinLODIndex);
			}
		}
	}

	return Result;
}

FInstancedSkinnedMeshSceneProxyDesc::FInstancedSkinnedMeshSceneProxyDesc(const UInstancedSkinnedMeshComponent* InComponent)
{
	InitializeFromInstancedSkinnedMeshComponent(InComponent);
}

void FInstancedSkinnedMeshSceneProxyDesc::InitializeFromInstancedSkinnedMeshComponent(const UInstancedSkinnedMeshComponent* InComponent)
{
	InitializeFromSkinnedMeshComponent(InComponent);

	TransformProvider = InComponent->TransformProvider;

	AnimationMinScreenSize = InComponent->AnimationMinScreenSize;
	InstanceMinDrawDistance = InComponent->InstanceMinDrawDistance;
	InstanceStartCullDistance = InComponent->InstanceStartCullDistance;
	InstanceEndCullDistance = InComponent->InstanceEndCullDistance;
#if WITH_EDITOR
	SelectedInstances = InComponent->SelectedInstances;
#endif

	bAllowAlwaysVisible = true;

	InstanceDataSceneProxy = InComponent->GetInstanceDataSceneProxy();
}
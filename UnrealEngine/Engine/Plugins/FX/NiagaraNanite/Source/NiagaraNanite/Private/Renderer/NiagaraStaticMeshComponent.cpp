// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraStaticMeshComponent.h"

#include "NaniteSceneProxy.h"
#include "GPUSceneWriter.h"
#include "PrimitiveSceneDesc.h"
#include "SceneInterface.h"

namespace NiagaraStaticMeshComponentPrivate
{
	class FNaniteSceneProxy : public Nanite::FSceneProxy
	{
	public:
		FNaniteSceneProxy(Nanite::FMaterialAudit& InNaniteMaterials, const FInstancedStaticMeshSceneProxyDesc& InDesc)
			: Nanite::FSceneProxy(InNaniteMaterials, InDesc)
		{
			bAlwaysHasVelocity = true;
		}
	};

	class FMeshSceneProxy : public FInstancedStaticMeshSceneProxy
	{
	public:
		FMeshSceneProxy(const FInstancedStaticMeshSceneProxyDesc& InDesc, ERHIFeatureLevel::Type InFeatureLevel)
			: FInstancedStaticMeshSceneProxy(InDesc, InFeatureLevel)
		{
			bAlwaysHasVelocity = true;
		}
	};
}

UNiagaraStaticMeshComponent::UNiagaraStaticMeshComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Disable unsupported rendering features (currently require instance data on CPU).
	bAffectDynamicIndirectLighting = false;
	bAffectDistanceFieldLighting = false;

	BodyInstance.bSimulatePhysics = false;
	SetGenerateOverlapEvents(false);

	bEnableVertexColorMeshPainting = false;

	bNavigationRelevant = false;
	bCanEverAffectNavigation = false;

	bEnableAutoLODGeneration = false;

	Mobility = EComponentMobility::Movable;
}

void UNiagaraStaticMeshComponent::OnComponentDestroyed(bool)
{
	PendingCpuUpdateFunction.Reset();
	//PendingGpuUpdateFunction.Reset();
}

FPrimitiveSceneProxy* UNiagaraStaticMeshComponent::CreateSceneProxy()
{
	if (NumInstances <= 0)
	{
		return nullptr;
	}
	return Super::CreateSceneProxy();
}

FMatrix UNiagaraStaticMeshComponent::GetRenderMatrix() const
{
	return GetComponentTransform().ToMatrixWithScale();
}

FBoxSphereBounds UNiagaraStaticMeshComponent::CalcBounds(const FTransform& BoundTransform) const
{
	// Update directly from the outer, we are at the exact same spot so the results are exactly the same
	if (USceneComponent* OuterComponent = GetTypedOuter<USceneComponent>())
	{
		return OuterComponent->Bounds;
	}

	// We should not enter this path
	const float DefaultBoundsExtent = 10.0f;
	const FBox LocalBounds = FBox(-FVector::OneVector * DefaultBoundsExtent, FVector::OneVector * DefaultBoundsExtent);
	return LocalBounds.TransformBy(BoundTransform);
}

#if WITH_EDITOR
FBox UNiagaraStaticMeshComponent::GetStreamingBounds() const
{
	return FBox::BuildAABB(Bounds.Origin, Bounds.BoxExtent);
}
#endif

bool UNiagaraStaticMeshComponent::BuildTextureStreamingDataImpl(ETextureStreamingBuildType BuildType, EMaterialQualityLevel::Type QualityLevel, ERHIFeatureLevel::Type FeatureLevel, TSet<FGuid>& DependentResources, bool& bOutSupportsBuildTextureStreamingData)
{
#if WITH_EDITORONLY_DATA // Only rebuild the data in editor 
	if (NumInstances > 0)
	{
		return Super::BuildTextureStreamingDataImpl(BuildType, QualityLevel, FeatureLevel, DependentResources, bOutSupportsBuildTextureStreamingData);
	}
#endif
	return true;
}

void UNiagaraStaticMeshComponent::GetStreamingRenderAssetInfo(FStreamingTextureLevelContext& LevelContext, TArray<FStreamingRenderAssetPrimitiveInfo>& OutStreamingRenderAssets) const
{
	// Don't only look the instance count but also if the bound is valid, as derived classes might not set PerInstanceSMData.
	if (NumInstances > 0 || Bounds.SphereRadius > 0)
	{
		return Super::GetStreamingRenderAssetInfo(LevelContext, OutStreamingRenderAssets);
	}
}

bool UNiagaraStaticMeshComponent::GetMaterialStreamingData(int32 MaterialIndex, FPrimitiveMaterialInfo& MaterialData) const
{
	// Same thing as StaticMesh but we take the full bounds to cover the instances.
	if (GetStaticMesh())
	{
		MaterialData.Material = GetMaterial(MaterialIndex);
		MaterialData.UVChannelData = GetStaticMesh()->GetUVChannelData(MaterialIndex);
		MaterialData.PackedRelativeBox = PackedRelativeBox_Identity;
	}
	return MaterialData.IsValid();
}

FPrimitiveSceneProxy* UNiagaraStaticMeshComponent::CreateStaticMeshSceneProxy(Nanite::FMaterialAudit& NaniteMaterials, bool bCreateNanite)
{
	LLM_SCOPE(ELLMTag::InstancedMesh);
	
	if (!ensure(GetWorld()) || !ensure(GetWorld()->Scene))
	{
		return nullptr;
	}
	
	if (!UseGPUScene(GetWorld()->Scene->GetShaderPlatform(), GetWorld()->Scene->GetFeatureLevel()))
	{
		return nullptr;
	}
	
	if (CheckPSOPrecachingAndBoostPriority() && GetPSOPrecacheProxyCreationStrategy() == EPSOPrecacheProxyCreationStrategy::DelayUntilPSOPrecached)
	{
		return nullptr;
	}
	
	FInstancedStaticMeshSceneProxyDesc Desc;
	GetSceneProxyDesc(Desc);

	if (bCreateNanite)
	{
		return ::new NiagaraStaticMeshComponentPrivate::FNaniteSceneProxy(NaniteMaterials, Desc);
	}
	else
	{
		//return ::new FInstancedStaticMeshSceneProxy(Desc, GetWorld()->GetFeatureLevel());
		return ::new NiagaraStaticMeshComponentPrivate::FMeshSceneProxy(Desc, GetWorld()->GetFeatureLevel());
	}
}

void UNiagaraStaticMeshComponent::GetSceneProxyDesc(FInstancedStaticMeshSceneProxyDesc& OutSceneProxyDesc) const
{
	FInstanceSceneDataBuffers InstanceSceneDataBuffers(bUseCpuOnlyUpdates == false);
	{
		FInstanceSceneDataBuffers::FAccessTag AccessTag(PointerHash(this));
		FInstanceSceneDataBuffers::FWriteView ProxyData = InstanceSceneDataBuffers.BeginWriteAccess(AccessTag);
	
		InstanceSceneDataBuffers.SetPrimitiveLocalToWorld(GetRenderMatrix(), AccessTag);

		ProxyData.NumCustomDataFloats = NumCustomDataFloats;
		ProxyData.InstanceLocalBounds.SetNum(1);
		ProxyData.InstanceLocalBounds[0] = GetStaticMesh()->GetBounds();

		ProxyData.Flags.bHasPerInstanceCustomData = ProxyData.NumCustomDataFloats > 0;
		ProxyData.Flags.bHasPerInstanceDynamicData = true;

		if (bUseCpuOnlyUpdates)
		{
			if (PendingCpuUpdateFunction.IsSet())
			{
				PendingCpuUpdateFunction(ProxyData);
			}
			else
			{
				ProxyData.InstanceToPrimitiveRelative.Empty();
				ProxyData.PrevInstanceToPrimitiveRelative.Empty();
			}
		}
		else
		{
			ProxyData.NumInstancesGPUOnly = NumInstances;
		}

		InstanceSceneDataBuffers.EndWriteAccess(AccessTag);
		InstanceSceneDataBuffers.ValidateData();
	}
	
	OutSceneProxyDesc.InitializeFromStaticMeshComponent(this);
	OutSceneProxyDesc.InstanceDataSceneProxy = MakeShared<FInstanceDataSceneProxy, ESPMode::ThreadSafe>(MoveTemp(InstanceSceneDataBuffers));
	OutSceneProxyDesc.bUseGpuLodSelection = true;
}

void UNiagaraStaticMeshComponent::UpdateInstanceCPU(int32 NumRequiredInstances, FCpuInstanceUpdateFunction UpdateFunction)
{
	check(bUseCpuOnlyUpdates == true);

	NumInstances = NumRequiredInstances;
	PendingCpuUpdateFunction = MoveTemp(UpdateFunction);
	MarkRenderStateDirty();
}

void UNiagaraStaticMeshComponent::UpdateInstanceGPU(int32 NumRequiredInstances, FGpuInstanceUpdateFunction UpdateFunction)
{
	check(bUseCpuOnlyUpdates == false);

	// Recreate immediately as we need to get the Proxy when sending the update over
	if (NumInstances < NumRequiredInstances)
	{
		NumInstances = NumRequiredInstances;
		RecreateRenderState_Concurrent();
	}

	NumInstances = NumRequiredInstances;

	UWorld* World = GetWorld();
	if (World && World->Scene)
	{
		FPrimitiveSceneDesc PrimitiveSceneDesc;
		PrimitiveSceneDesc.SceneProxy = GetSceneProxy();
		if (PrimitiveSceneDesc.SceneProxy)
		{
			World->Scene->UpdatePrimitiveInstancesFromCompute(
				&PrimitiveSceneDesc,
				FGPUSceneWriteDelegate::CreateLambda(
					[UpdateFunction_RT=MoveTemp(UpdateFunction)](FRDGBuilder& GraphBuilder, const FGPUSceneWriteDelegateParams& GpuSceneParams)
					{
						UpdateFunction_RT(GraphBuilder, GpuSceneParams);
					}
				)
			);
		}
	}
}

// Copyright Epic Games, Inc. All Rights Reserved.

#include "DynamicWindData.h"
#include "DynamicWindSkeletalData.h"
#include "DynamicWindProvider.h"
#include "RenderingThread.h"
#include "RenderUtils.h"
#include "ScenePrivate.h"
#include "SkeletalRenderPublic.h"
#include "SkinningDefinitions.h"
#include "Components/InstancedSkinnedMeshComponent.h"

bool UDynamicWindData::IsEnabled() const
{
	return bEnabled;
}

const FGuid& UDynamicWindData::GetTransformProviderID() const
{
	static FGuid DynamicWindProviderId(DYNAMIC_WIND_TRANSFORM_PROVIDER_GUID);
	return DynamicWindProviderId;
}

bool UDynamicWindData::UsesSkeletonBatching() const
{
	return true;
}

const uint32 UDynamicWindData::GetUniqueAnimationCount() const
{
	return DYNAMIC_WIND_DIRECTIONALITY_SLICES;
}

bool UDynamicWindData::HasAnimationBounds() const
{
	return false;
}

bool UDynamicWindData::GetAnimationBounds(uint32 AnimationIndex, FRenderBounds& OutBounds) const
{
	return false;
}

uint32 UDynamicWindData::GetSkinningDataOffset(int32 InstanceIndex, const FTransform& ComponentTransform, const FSkinnedMeshInstanceData& InstanceData) const
{
	FTransform WorldSpaceTransform = FTransform(InstanceData.Transform) * ComponentTransform;
	const FVector EulerRotation = WorldSpaceTransform.GetRotation().Euler();
	int32 DirectionalityIndex = 0;
	{
		const int32 ZRoundDown = FMath::RoundToInt32(float(EulerRotation.Z) / 45.0f) * 45u;
		DirectionalityIndex = ((ZRoundDown + 360u) % 360u) / 45u;
	}

	return DirectionalityIndex * 2u;
}

FTransformProviderRenderProxy* UDynamicWindData::CreateRenderThreadResources(FSkinningSceneExtensionProxy* SceneProxy, FSceneInterface& Scene, FRHICommandListBase& RHICmdList)
{
	FDynamicWindDataRenderProxy* ProviderProxy = new FDynamicWindDataRenderProxy(this, SceneProxy, Scene);
	ProviderProxy->CreateRenderThreadResources(RHICmdList);
	return ProviderProxy;
}

void UDynamicWindData::DestroyRenderThreadResources(FTransformProviderRenderProxy* ProviderProxy)
{
	ProviderProxy->DestroyRenderThreadResources();
	delete ProviderProxy;
}

FDynamicWindDataRenderProxy::FDynamicWindDataRenderProxy(UDynamicWindData* BankData, FSkinningSceneExtensionProxy* InSceneProxy, FSceneInterface& InScene)
: SceneProxy(InSceneProxy)
, Scene(InScene)
{
	UniqueAnimationCount = SceneProxy->GetUniqueAnimationCount();
	const USkinnedAsset* SkinnedAsset = SceneProxy->GetSkinnedAsset();
	check(SkinnedAsset != nullptr);
	
	const USkeletalMesh* SkeletalMesh = CastChecked<USkeletalMesh>(SkinnedAsset);
	SkeletalData = const_cast<USkeletalMesh*>(SkeletalMesh)->GetAssetUserData<UDynamicWindSkeletalData>();

	// GW-TODO: Need a validation method on the base data type to check per-proxy if ref pose fallback is needed.
}

FDynamicWindDataRenderProxy::~FDynamicWindDataRenderProxy()
{
}

void FDynamicWindDataRenderProxy::CreateRenderThreadResources(FRHICommandListBase& RHICmdList)
{
}

void FDynamicWindDataRenderProxy::DestroyRenderThreadResources()
{
}

const TConstArrayView<uint64> FDynamicWindDataRenderProxy::GetProviderData(bool& bOutValid) const
{
	bOutValid = true;
	return {};
}

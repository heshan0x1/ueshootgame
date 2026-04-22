// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Animation/TransformProviderData.h"

#include "DynamicWindData.generated.h"

UCLASS(hidecategories=Object, MinimalAPI, BlueprintType)
class UDynamicWindData : public UTransformProviderData
{
	GENERATED_BODY()

public:
	virtual bool IsEnabled() const override;
	virtual const FGuid& GetTransformProviderID() const override;
	virtual bool UsesSkeletonBatching() const override;
	virtual const uint32 GetUniqueAnimationCount() const override;
	virtual bool HasAnimationBounds() const override;
	virtual bool GetAnimationBounds(uint32 AnimationIndex, FRenderBounds& OutBounds) const override;
	virtual uint32 GetSkinningDataOffset(int32 InstanceIndex, const FTransform& ComponentTransform, const FSkinnedMeshInstanceData& InstanceData) const override;
	virtual FTransformProviderRenderProxy* CreateRenderThreadResources(FSkinningSceneExtensionProxy* SceneProxy, FSceneInterface& Scene, FRHICommandListBase& RHICmdList) override;
	virtual void DestroyRenderThreadResources(FTransformProviderRenderProxy* ProviderProxy) override;
};

class FDynamicWindDataRenderProxy : public FTransformProviderRenderProxy
{
	friend class UDynamicWindData;

public:
	DYNAMICWIND_API FDynamicWindDataRenderProxy(UDynamicWindData* BankData, FSkinningSceneExtensionProxy* SceneProxy, FSceneInterface& Scene);
	DYNAMICWIND_API virtual ~FDynamicWindDataRenderProxy();
	DYNAMICWIND_API virtual void CreateRenderThreadResources(FRHICommandListBase& RHICmdList) override;
	DYNAMICWIND_API virtual void DestroyRenderThreadResources() override;
	DYNAMICWIND_API virtual const TConstArrayView<uint64> GetProviderData(bool& bOutValid) const override;

private:
	FSkinningSceneExtensionProxy* SceneProxy = nullptr;
	TObjectPtr<const class UDynamicWindSkeletalData> SkeletalData = nullptr;
	FSceneInterface& Scene; 
	uint32 UniqueAnimationCount = 0;
};

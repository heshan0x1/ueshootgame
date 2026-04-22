// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "Misc/Guid.h"

#include "TransformProviderData.generated.h"

struct FRenderBounds;
struct FSkinnedMeshInstanceData;
class FSkinningSceneExtensionProxy;
class FSceneInterface;
class FRHICommandListBase;
class ITargetPlatform;

class FTransformProviderRenderProxy
{
public:
	FTransformProviderRenderProxy()
	{
	}

	virtual ~FTransformProviderRenderProxy()
	{
	}

	virtual void CreateRenderThreadResources(FRHICommandListBase& RHICmdList) = 0;

	virtual void DestroyRenderThreadResources() = 0;

	virtual const TConstArrayView<uint64> GetProviderData(bool& bOutValid) const = 0;
};

UCLASS(Abstract, config=Engine, hidecategories=Object, MinimalAPI, BlueprintType)
class UTransformProviderData : public UObject
{
	GENERATED_BODY()

public:
	virtual bool IsEnabled() const
	{
		return bEnabled;
	}

	virtual const FGuid& GetTransformProviderID() const
	{
		static FGuid InvalidID(0, 0, 0, 0);
		return InvalidID;
	}

	virtual const uint32 GetUniqueAnimationCount() const
	{
		return 1u;
	}

	virtual bool UsesSkeletonBatching() const
	{
		return false;
	}

	virtual bool HasAnimationBounds() const
	{
		return false;
	}

	virtual bool GetAnimationBounds(uint32 AnimationIndex, FRenderBounds& OutBounds) const
	{
		return false;
	}

	virtual uint32 GetSkinningDataOffset(int32 InstanceIndex, const FTransform& ComponentTransform, const FSkinnedMeshInstanceData& InstanceData) const
	{
		return 0u;
	}

	virtual FTransformProviderRenderProxy* CreateRenderThreadResources(FSkinningSceneExtensionProxy* SceneProxy, FSceneInterface& Scene, FRHICommandListBase& RHICmdList)
	{
		return nullptr;
	}

	virtual void DestroyRenderThreadResources(FTransformProviderRenderProxy* ProviderProxy)
	{
	}

	virtual bool IsCompiling() const
	{
		return false;
	}

#if WITH_EDITOR
	void BeginCacheForCookedPlatformData(const ITargetPlatform* TargetPlatform)
	{
	}

	bool IsCachedCookedPlatformDataLoaded(const ITargetPlatform* TargetPlatform)
	{
		return true;
	}
#endif

public:
	UPROPERTY(EditAnywhere, Category = TransformProvider)
	bool bEnabled;
};

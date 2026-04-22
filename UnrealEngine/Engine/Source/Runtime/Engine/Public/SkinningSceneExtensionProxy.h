// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PrimitiveSceneProxy.h"

class FSkeletalMeshObject;
class USkinnedAsset;
class FSkeletalMeshRenderData;
class UTransformProviderData;
class FTransformProviderRenderProxy;
struct FSkinnedMeshSceneProxyDesc;
struct FInstancedSkinnedMeshSceneProxyDesc;

class FSkinningSceneExtensionProxy
{
public:
	ENGINE_API FSkinningSceneExtensionProxy(FSkeletalMeshObject* InMeshObject, const USkinnedAsset* InSkinnedAsset, bool bAllowScaling);

	virtual ~FSkinningSceneExtensionProxy() = default;

	inline const USkinnedAsset* GetSkinnedAsset() const
	{
		return SkinnedAsset;
	}

	const FSkeletalMeshObject* GetMeshObject() const
	{
		return MeshObject;
	}

	TConstArrayView<uint32> GetBoneHierarchy() const
	{
		return BoneHierarchy;
	}

	TConstArrayView<float> GetBoneObjectSpace() const
	{
		return BoneObjectSpace;
	}

	uint32 GetMaxBoneTransformCount() const
	{
		return MaxBoneTransformCount;
	}

	uint32 GetMaxBoneHierarchyCount() const
	{
		return MaxBoneTransformCount;
	}

	uint32 GetMaxBoneObjectSpaceCount() const
	{
		return UseSectionBoneMap() ? 0 : MaxBoneTransformCount;
	}

	uint32 GetMaxBoneInfluenceCount() const
	{
		return MaxBoneInfluenceCount;
	}

	uint32 GetUniqueAnimationCount() const
	{
		return UniqueAnimationCount;
	}

	bool HasScale() const
	{
		return bHasScale;
	}

	bool UseSkeletonBatching() const
	{
		return bUseSkeletonBatching;
	}

	bool UseSectionBoneMap() const
	{
		return bUseSectionBoneMap;
	}

	bool UseInstancing() const
	{
		return bUseInstancing;
	}

	// TODO: TEMP - Move to shared location with GPU
	uint32 GetObjectSpaceFloatCount() const
	{
		return 4 /* quat */ + 3 /* XYZ translation */ + (HasScale() ? 3 : 0 /* XYZ scale */);
	}

	virtual void CreateRenderThreadResources(FSceneInterface& Scene, FRHICommandListBase& RHICmdList) {}

	virtual void DestroyRenderThreadResources() {}

	virtual const FGuid& GetTransformProviderId() const;

	virtual TConstArrayView<uint64> GetAnimationProviderData(bool &bOutValid) const;

protected:
	const USkinnedAsset* SkinnedAsset = nullptr;
	FSkeletalMeshObject* MeshObject = nullptr;

	TArray<uint32> BoneHierarchy;
	TArray<float> BoneObjectSpace;

	uint16 MaxBoneTransformCount        = 0u;
	uint16 MaxBoneInfluenceCount        = 0u;
	uint16 UniqueAnimationCount         = 1u;

	uint8 bHasScale            : 1 = false;
	uint8 bUseSkeletonBatching : 1 = false;
	uint8 bUseSectionBoneMap   : 1 = false;
	uint8 bUseInstancing       : 1 = false;
};

class FInstancedSkinningSceneExtensionProxy : public FSkinningSceneExtensionProxy
{
public:
	ENGINE_API FInstancedSkinningSceneExtensionProxy(TObjectPtr<UTransformProviderData> InTransformProvider, FSkeletalMeshObject* InMeshObject, const USkinnedAsset* InSkinnedAsset, bool bAllowScaling);

	ENGINE_API void CreateRenderThreadResources(FSceneInterface& Scene, FRHICommandListBase& RHICmdList);
	ENGINE_API void DestroyRenderThreadResources();

	ENGINE_API TConstArrayView<uint64> GetAnimationProviderData(bool& bOutValid) const override;

	ENGINE_API const FGuid& GetTransformProviderId() const override;

protected:
	TObjectPtr<UTransformProviderData> TransformProvider;
	FTransformProviderRenderProxy* TransformProviderProxy = nullptr;
	FGuid TransformProviderId;
};
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MaterialCacheMeshProcessor.h"
#include "MaterialCacheSceneData.h"
#include "SceneExtensions.h"
#include "PrimitiveComponentId.h"

class FPrimitiveSceneProxy;
class IAllocatedVirtualTexture;
struct FMaterialCacheSceneExtensionData;
struct FMaterialCachePrimitiveData;
struct FMaterialCacheProviderData;
class UMaterialCacheVirtualTextureTag;

class FMaterialCacheSceneExtension : public ISceneExtension
{
	DECLARE_SCENE_EXTENSION(RENDERER_API, FMaterialCacheSceneExtension);

public:
	FMaterialCacheSceneExtension(FScene& InScene);

	/** Get the primitive data associated with a primitive id, nullptr if not found */
	FMaterialCachePrimitiveData* GetPrimitiveData(FPrimitiveComponentId PrimitiveComponentId) const;

	/** Clear all cached primitive command data */
	void ClearCachedPrimitiveData();

public:
	/** All pending tags, lifetime tied to the scene's renderer */
	TMap<FGuid, FMaterialCachePendingTagBucket> TagBuckets;
	
public: /** ISceneExtension */
	static bool ShouldCreateExtension(FScene& Scene);
	virtual ISceneExtensionUpdater* CreateUpdater() override;

private:
	TUniquePtr<FMaterialCacheSceneExtensionData> Data;
};

// Copyright Epic Games, Inc. All Rights Reserved.

#include "MaterialCache/MaterialCacheSceneExtension.h"
#include "MaterialCache/MaterialCache.h"
#include "GlobalRenderResources.h"
#include "MaterialCache/MaterialCachePrimitiveData.h"
#include "MaterialCache/MaterialCacheTagProvider.h"
#include "MaterialCache/MaterialCacheVirtualTextureRenderProxy.h"
#include "MaterialCache/MaterialCacheVirtualTextureTag.h"
#include "ShaderParameterMacros.h"
#include "MaterialCacheDefinitions.h"
#include "ScenePrivate.h"

IMPLEMENT_SCENE_EXTENSION(FMaterialCacheSceneExtension);

struct FMaterialCacheSceneExtensionData
{
	~FMaterialCacheSceneExtensionData()
	{
		checkf(SceneDataMap.IsEmpty(), TEXT("Released scene extension data with dangling references"));
	}
	
	FCriticalSection CriticalSection;

	/** Shared primitive data map */
	TMap<FPrimitiveComponentId, FMaterialCachePrimitiveData> SceneDataMap;
};

class FMaterialCacheSceneExtensionUpdater : public ISceneExtensionUpdater
{
	DECLARE_SCENE_EXTENSION_UPDATER(FRenderer, FMaterialCacheSceneExtension);

public:
	FMaterialCacheSceneExtensionUpdater(FScene& InScene, FMaterialCacheSceneExtensionData& Data) : Scene(InScene), Data(Data)
	{
		
	}
	
	virtual void PreSceneUpdate(FRDGBuilder& GraphBuilder, const FScenePreUpdateChangeSet& ChangeSet, FSceneUniformBuffer& SceneUniforms) override
	{
		FMaterialCacheTagProvider& TagProvider = FMaterialCacheTagProvider::Get();

		// Process all remoted primitives
		for (FPrimitiveSceneInfo* PrimitiveSceneInfo : ChangeSet.RemovedPrimitiveSceneInfos)
		{
			FPrimitiveSceneProxy* Proxy = PrimitiveSceneInfo->Proxy;
			if (!Proxy || !Proxy->SupportsMaterialCache())
			{
				continue;
			}

			// Free the primitive tag offset
			TagProvider.FreePrimitiveTagOffset(Proxy->MaterialCacheDescriptor);
			Proxy->MaterialCacheDescriptor = UINT32_MAX;

			// Remove from tracked primitives
			Data.SceneDataMap.Remove(Proxy->GetPrimitiveComponentId());
		}
	}

	virtual void PostSceneUpdate(FRDGBuilder& GraphBuilder, const FScenePostUpdateChangeSet& ChangeSet) override
	{
		FMaterialCacheTagProvider& TagProvider = FMaterialCacheTagProvider::Get();

		// Process all added primitives
		for (FPrimitiveSceneInfo* PrimitiveSceneInfo : ChangeSet.AddedPrimitiveSceneInfos)
		{
			FPrimitiveSceneProxy* Proxy = PrimitiveSceneInfo->Proxy;
			if (!Proxy || !Proxy->SupportsMaterialCache())
			{
				continue;
			}

			// Allocate the primitive tag offset
			checkf(Proxy->MaterialCacheDescriptor == UINT32_MAX, TEXT("Primitive double-registration"));
			Proxy->MaterialCacheDescriptor = TagProvider.AllocatePrimitiveTagOffset();

			// Shouldn't be tracking this
			checkf(!Data.SceneDataMap.Contains(Proxy->GetPrimitiveComponentId()), TEXT("Dangling primitive scene data"));

			// Associate proxy to CID
			FMaterialCachePrimitiveData& PrimitiveData = Data.SceneDataMap.Add(Proxy->GetPrimitiveComponentId());
			PrimitiveData.Proxy = Proxy;

			// Register all tag entries for the primitive
			for (FMaterialCacheVirtualTextureRenderProxy* MaterialCacheProxy : Proxy->MaterialCacheRenderProxies)
			{
				if (ensure(MaterialCacheProxy))
				{
					UE::HLSL::FMaterialCacheTagEntry Entry;
					Entry.PackedUniform = MaterialCacheProxy->TextureDescriptor;
					TagProvider.SetTagEntry(Proxy->MaterialCacheDescriptor, MaterialCacheProxy->TagGuid, Entry);
				}
			}
		}
	}

private:
	FScene& Scene;
	
	FMaterialCacheSceneExtensionData& Data;
};

FMaterialCacheSceneExtension::FMaterialCacheSceneExtension(FScene& InScene) : ISceneExtension(InScene)
{
	Data = MakeUnique<FMaterialCacheSceneExtensionData>();
}

bool FMaterialCacheSceneExtension::ShouldCreateExtension(FScene& Scene)
{
	return IsMaterialCacheEnabled(Scene.GetShaderPlatform());
}

ISceneExtensionUpdater* FMaterialCacheSceneExtension::CreateUpdater()
{
	return new FMaterialCacheSceneExtensionUpdater(Scene, *Data);
}

FMaterialCachePrimitiveData* FMaterialCacheSceneExtension::GetPrimitiveData(FPrimitiveComponentId PrimitiveComponentId) const
{
	// Multi-consumer is fine
	check(IsInParallelRenderingThread());
	return Data->SceneDataMap.Find(PrimitiveComponentId);
}

void FMaterialCacheSceneExtension::ClearCachedPrimitiveData()
{
	// Remove all cached commands for all tags
	for (auto&& [_, PrimitiveData] : Data->SceneDataMap)
	{
		PrimitiveData.CachedCommands.Tags.Empty();
	}
}

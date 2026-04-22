// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MaterialCacheMeshProcessor.h"
#include "PrimitiveComponentId.h"

class FPrimitiveSceneProxy;

struct FMaterialCachePrimitiveCachedLayerCommands
{
	FMaterialCachePrimitiveCachedLayerCommands() = default;

	/** No copy or move construction */
	UE_NONCOPYABLE(FMaterialCachePrimitiveCachedLayerCommands);
	
	TArray<FMaterialCacheMeshDrawCommand>          StaticMeshBatchCommands;
	TOptional<FMaterialCacheLayerShadingCSCommand> NaniteLayerShadingCommand;
	TOptional<FMaterialCacheLayerShadingCSCommand> VertexInvariantShadingCommand;
};

struct FMaterialCachePrimitiveCachedTagCommands
{
	/** Lifetime of material tied to the proxy, any change invalidates the proxy, in turn clearing the cache */
	TMap<UMaterialInterface*, TUniquePtr<FMaterialCachePrimitiveCachedLayerCommands>> Layers;
};

struct FMaterialCachePrimitiveCachedCommands
{
	/** All cached material layers for a given teg */
	TMap<FGuid, FMaterialCachePrimitiveCachedTagCommands> Tags;
};

struct FMaterialCachePrimitiveData
{
	/** Proxy tied to the primitive */
	FPrimitiveSceneProxy* Proxy = nullptr;

	/** All cached commands */
	FMaterialCachePrimitiveCachedCommands CachedCommands;
};

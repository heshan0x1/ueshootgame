// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MaterialCache/MaterialCacheStackProvider.h"
#include "Templates/UniquePtr.h"
#include "PrimitiveComponentId.h"
#include "Math/MathFwd.h"

class FSceneInterface;
class FMaterialCacheStackProviderRenderProxy;

class FMaterialCacheVirtualTextureRenderProxy
{
public:
	/** Flush all pages of this texture */
	ENGINE_API void Flush(FSceneInterface* Scene);

public:
	/** Component owning the virtual texture */
	FPrimitiveComponentId PrimitiveCID{};

	/** Optional, tag of this texture */
	FGuid TagGuid;

	/** Optional, owned stack provider render proxy */
	TUniquePtr<FMaterialCacheStackProviderRenderProxy> StackProviderRenderProxy;

	/** Packed virtual texturing descriptor */
	FUintVector2 TextureDescriptor = FUintVector2(0, 0);

	/** UV channel used for unwrapping */
	uint32 UVCoordinateIndex = 0;
};

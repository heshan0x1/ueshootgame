// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RHIShaderPlatform.h"

/** Get the width of a tile */
ENGINE_API uint32 GetMaterialCacheTileWidth();

/** Get the border width of a tile */
ENGINE_API uint32 GetMaterialCacheTileBorderWidth();

/** Is object space supported (e.g., cook data) for the given platform? */
ENGINE_API bool IsMaterialCacheSupported(FStaticShaderPlatform Platform);

/** Is object space enabled at runtime? May be toggled. */
ENGINE_API bool IsMaterialCacheEnabled(FStaticShaderPlatform Platform);

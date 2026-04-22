// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NaniteShared.h"
#include "NaniteCullRaster.h"
#include "MeshPassProcessor.h"
#include "GBufferInfo.h"

struct FNaniteShadingCommands;
class  FLumenCardPassUniformParameters;
class  FCardPageRenderData;
class  FSceneRenderer;

struct FNaniteMaterialSlot
{
	struct FPacked
	{
		uint32 Data[2];
	};

	FNaniteMaterialSlot()
	: TriangleShadingBin(0xFFFF)
	, VoxelShadingBin(0xFFFF)
	, RasterBin(0xFFFF)
	, FallbackRasterBin(0xFFFF)
	{
	}

	inline FPacked Pack() const
	{
		FPacked Ret;
		Ret.Data[0] = (TriangleShadingBin | (VoxelShadingBin << 16u));
		Ret.Data[1] = (RasterBin | (FallbackRasterBin << 16u));
		return Ret;
	}

	uint16 TriangleShadingBin;
	uint16 VoxelShadingBin;
	uint16 RasterBin;
	uint16 FallbackRasterBin;
};

struct FNaniteMaterialDebugViewInfo
{
#if WITH_DEBUG_VIEW_MODES
	struct FPacked
	{
		uint32 Data[3];
	};

	FNaniteMaterialDebugViewInfo()
	: InstructionCountVS(0)
	, InstructionCountPS(0)
	, InstructionCountCS(0)
	, LWCComplexityVS(0)
	, LWCComplexityPS(0)
	, LWCComplexityCS(0)
	{
	}

	uint16 InstructionCountVS;
	uint16 InstructionCountPS;
	uint16 InstructionCountCS;

	uint16 LWCComplexityVS;
	uint16 LWCComplexityPS;
	uint16 LWCComplexityCS;

	FPacked Pack() const
	{
		FPacked Result;
		Result.Data[0] = static_cast<uint32>(InstructionCountPS)	<< 16u | static_cast<uint32>(InstructionCountVS);
		Result.Data[1] = static_cast<uint32>(LWCComplexityVS)		<< 16u | static_cast<uint32>(InstructionCountCS);
		Result.Data[2] = static_cast<uint32>(LWCComplexityPS)		<< 16u | static_cast<uint32>(LWCComplexityCS);
		return Result;
	}
#endif
};

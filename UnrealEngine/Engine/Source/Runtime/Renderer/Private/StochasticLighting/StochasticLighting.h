// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "RenderGraphFwd.h"
#include "Lumen/LumenFrontLayerTranslucency.h"

class FViewInfo;
class FSceneViewState;
struct FMinimalSceneTextures;
enum class EReflectionsMethod;

namespace StochasticLighting
{
	enum class EMaterialSource
	{
		GBuffer,
		FrontLayerGBuffer,
		HairStrands,
		MAX
	};

	enum class EStochasticSampleOffset
	{
		None,
		DownsampleFactor2x1,
		DownsampleFactor2x2,
		Both,
		MAX
	};

	BEGIN_SHADER_PARAMETER_STRUCT(FHistoryScreenParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, EncodedHistoryScreenCoordTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, PackedPixelDataTexture)
		SHADER_PARAMETER(FVector4f, HistoryScreenPositionScaleBias)
		SHADER_PARAMETER(FVector4f, HistoryUVMinMax)
		SHADER_PARAMETER(FVector4f, HistoryGatherUVMinMax)
		SHADER_PARAMETER(FVector4f, HistoryBufferSizeAndInvSize)
		SHADER_PARAMETER(FVector4f, HistorySubPixelGridSizeAndInvSize)
		SHADER_PARAMETER(FIntPoint, HistoryScreenCoordDecodeShift)
	END_SHADER_PARAMETER_STRUCT()

	int32 GetStateFrameIndex(const FSceneViewState* ViewState);

	bool IsStateFrameIndexOverridden();

	void InitDefaultHistoryScreenParameters(FHistoryScreenParameters& OutHistoryScreenParameters);

	struct FRunConfig
	{
		ERDGPassFlags ComputePassFlags = ERDGPassFlags::Compute;
		int32 StateFrameIndexOverride = -1;
		bool bSubstrateOverflow = false;
		bool bCopyDepthAndNormal = false;
		bool bDownsampleDepthAndNormal2x1 = false;
		bool bDownsampleDepthAndNormal2x2 = false;
		bool bTileClassifyLumen = false;
		bool bTileClassifyMegaLights = false;
		bool bTileClassifySubstrate = false;
		bool bReprojectLumen = false;
		bool bReprojectMegaLights = false;
	};

	struct FContext
	{
		FRDGBuilder& GraphBuilder;
		const FMinimalSceneTextures& SceneTextures;
		const FLumenFrontLayerTranslucencyGBufferParameters& FrontLayerTranslucencyGBuffer;
		StochasticLighting::EMaterialSource MaterialSource;
		FRDGTextureUAVRef DepthHistoryUAV = nullptr;
		FRDGTextureUAVRef NormalHistoryUAV = nullptr;
		FRDGTextureUAVRef DownsampledSceneDepth2x1UAV = nullptr;
		FRDGTextureUAVRef DownsampledWorldNormal2x1UAV = nullptr;
		FRDGTextureUAVRef DownsampledSceneDepth2x2UAV = nullptr;
		FRDGTextureUAVRef DownsampledWorldNormal2x2UAV = nullptr;
		FRDGTextureUAVRef LumenTileBitmaskUAV = nullptr;
		FRDGTextureUAVRef MegaLightsTileBitmaskUAV = nullptr;
		FRDGTextureUAVRef EncodedHistoryScreenCoordUAV = nullptr;
		FRDGTextureUAVRef LumenPackedPixelDataUAV = nullptr;
		FRDGTextureUAVRef MegaLightsPackedPixelDataUAV = nullptr;
		// Only non-resource parameters will be populated
		FHistoryScreenParameters* OutHistoryScreenParameters = nullptr;

		FContext(
			FRDGBuilder& InGraphBuilder,
			const FMinimalSceneTextures& InSceneTextures,
			const FLumenFrontLayerTranslucencyGBufferParameters& InFrontLayerTranslucencyGBuffer,
			StochasticLighting::EMaterialSource InMaterialSource);

		void Validate(const FRunConfig& RunConfig) const;

		void Run(const FViewInfo& View, EReflectionsMethod ViewReflectionsMethod, const FRunConfig& RunConfig);
	};
}
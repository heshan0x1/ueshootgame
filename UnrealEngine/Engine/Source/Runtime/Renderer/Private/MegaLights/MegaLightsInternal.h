// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RendererPrivate.h"
#include "BlueNoise.h"
#include "MegaLightsDefinitions.h"
#include "StochasticLighting/StochasticLighting.h"

BEGIN_SHADER_PARAMETER_STRUCT(FMegaLightsParameters, )
	SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FHairStrandsViewUniformParameters, HairStrands)
	SHADER_PARAMETER_STRUCT_INCLUDE(ShaderPrint::FShaderParameters, ShaderPrintUniformBuffer)
	SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureParameters, SceneTextures)
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSceneUniformParameters, Scene)
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSceneTextureUniformParameters, SceneTexturesStruct)
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSubstrateGlobalUniformParameters, Substrate)
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FForwardLightUniformParameters, ForwardLightStruct)
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(LightFunctionAtlas::FLightFunctionAtlasGlobalParameters, LightFunctionAtlas)
	SHADER_PARAMETER_STRUCT_INCLUDE(FSceneLightingChannelParameters, LightingChannelParameters)
	SHADER_PARAMETER_STRUCT_REF(FBlueNoise, BlueNoise)
	SHADER_PARAMETER_TEXTURE(Texture2D, PreIntegratedGF)
	SHADER_PARAMETER_SAMPLER(SamplerState, PreIntegratedGFSampler)
	SHADER_PARAMETER(FIntPoint, SampleViewMin)
	SHADER_PARAMETER(FIntPoint, SampleViewSize)
	SHADER_PARAMETER(FIntPoint, DownsampledViewMin)
	SHADER_PARAMETER(FIntPoint, DownsampledViewSize)
	SHADER_PARAMETER(FIntPoint, NumSamplesPerPixel)
	SHADER_PARAMETER(FIntPoint, NumSamplesPerPixelDivideShift)
	SHADER_PARAMETER(FVector2f, DownsampledBufferInvSize)	
	SHADER_PARAMETER(FIntPoint, DownsampleFactor)
	SHADER_PARAMETER(uint32, MegaLightsStateFrameIndex)
	SHADER_PARAMETER(uint32, StochasticLightingStateFrameIndex)
	SHADER_PARAMETER(float, MinSampleWeight)
	SHADER_PARAMETER(float, MaxShadingWeight)
	SHADER_PARAMETER(int32, TileDataStride)
	SHADER_PARAMETER(int32, DownsampledTileDataStride)
	SHADER_PARAMETER(int32, DebugMode)
	SHADER_PARAMETER(FIntPoint, DebugCursorPosition)
	SHADER_PARAMETER(int32, DebugLightId)
	SHADER_PARAMETER(int32, DebugVisualizeLight)
	SHADER_PARAMETER(int32, UseIESProfiles)
	SHADER_PARAMETER(int32, UseLightFunctionAtlas)
	SHADER_PARAMETER(FMatrix44f, UnjitteredClipToTranslatedWorld)
	SHADER_PARAMETER(FMatrix44f, UnjitteredTranslatedWorldToClip)
	SHADER_PARAMETER(FMatrix44f, UnjitteredPrevTranslatedWorldToClip)
	SHADER_PARAMETER_STRUCT_INCLUDE(FHZBParameters, HZBParameters)
	SHADER_PARAMETER(FIntPoint, VisibleLightHashViewMinInTiles)
	SHADER_PARAMETER(FIntPoint, VisibleLightHashViewSizeInTiles)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, DownsampledSceneDepth)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D<UNORM float3>, DownsampledSceneWorldNormal)
END_SHADER_PARAMETER_STRUCT()

BEGIN_SHADER_PARAMETER_STRUCT(FMegaLightsVolumeParameters, )
	SHADER_PARAMETER(float, VolumeMinSampleWeight)
	SHADER_PARAMETER(float, VolumeMaxShadingWeight)
	SHADER_PARAMETER(int32, VolumeDownsampleFactorMultShift)
	SHADER_PARAMETER(int32, VolumeDebugMode)
	SHADER_PARAMETER(int32, VolumeDebugSliceIndex)
	SHADER_PARAMETER(FIntVector, NumSamplesPerVoxel)
	SHADER_PARAMETER(FIntVector, NumSamplesPerVoxelDivideShift)
	SHADER_PARAMETER(FIntVector, DownsampledVolumeViewSize)
	SHADER_PARAMETER(FIntVector, VolumeViewSize)
	SHADER_PARAMETER(FIntVector, VolumeSampleViewSize)
	SHADER_PARAMETER(FVector3f, VolumeInvBufferSize)
	SHADER_PARAMETER(FVector3f, MegaLightsVolumeZParams)
	SHADER_PARAMETER(uint32, MegaLightsVolumePixelSize)
	SHADER_PARAMETER(FVector3f, VolumeFrameJitterOffset)
	SHADER_PARAMETER(float, VolumePhaseG)
	SHADER_PARAMETER(float, VolumeInverseSquaredLightDistanceBiasScale)
	SHADER_PARAMETER(float, LightSoftFading)
	SHADER_PARAMETER(uint32, TranslucencyVolumeCascadeIndex)
	SHADER_PARAMETER(float, TranslucencyVolumeInvResolution)
	SHADER_PARAMETER(uint32, UseHZBOcclusionTest)
	SHADER_PARAMETER(uint32, IsUnifiedVolume)
	SHADER_PARAMETER(FIntVector, ResampleVolumeViewSize)
	SHADER_PARAMETER(FVector3f, ResampleVolumeInvBufferSize)
	SHADER_PARAMETER(FVector3f, ResampleVolumeZParams)
END_SHADER_PARAMETER_STRUCT()

enum class EMegaLightsInput : uint8
{
	GBuffer,
	HairStrands,
	Count
};

// Internal functions, don't use outside of the MegaLights
namespace MegaLights
{
	void RayTraceLightSamples(
		const FSceneViewFamily& ViewFamily,
		const FViewInfo& View, int32 ViewIndex,
		FRDGBuilder& GraphBuilder,
		const FSceneTextures& SceneTextures,
		const FVirtualShadowMapArray* VirtualShadowMapArray,
		const TArrayView<FRDGTextureRef> NaniteShadingMasks,
		const FIntPoint SampleBufferSize,
		FRDGTextureRef LightSamples,
		FRDGTextureRef LightSampleRays,
		FIntVector VolumeSampleBufferSize,
		FRDGTextureRef VolumeLightSamples,
		FRDGTextureRef VolumeLightSampleRays,
		FIntVector TranslucencyVolumeSampleBufferSize,
		TArrayView<FRDGTextureRef> TranslucencyVolumeLightSamples,
		TArrayView<FRDGTextureRef> TranslucencyVolumeLightSampleRays,
		const FMegaLightsParameters& MegaLightsParameters,
		const FMegaLightsVolumeParameters& MegaLightsVolumeParameters,
		const FMegaLightsVolumeParameters& MegaLightsTranslucencyVolumeParameters,
		EMegaLightsInput InputType,
		bool bDebug
	);

	void MarkVSMPages(
		const FViewInfo& View, int32 ViewIndex,
		FRDGBuilder& GraphBuilder,
		const FVirtualShadowMapArray& VirtualShadowMapArray,
		const FIntPoint SampleBufferSize,
		FRDGTextureRef LightSamples,
		FRDGTextureRef LightSampleRays,
		const FMegaLightsParameters& MegaLightsParameters,
		EMegaLightsInput InputType);

	bool UseWaveOps(EShaderPlatform ShaderPlatform);
	EPixelFormat GetLightingDataFormat();

	bool IsDebugEnabledForShadingPass(int32 ShadingPassIndex, EShaderPlatform InPlatform);
	int32 GetDebugMode(EMegaLightsInput Input);

	FIntPoint GetNumSamplesPerPixel2d(EMegaLightsInput InputType);
	FIntPoint GetNumSamplesPerPixel2d(int32 NumSamplesPerPixel1d);
	FIntVector GetNumSamplesPerVoxel3d(int32 NumSamplesPerVoxel1d);

	bool SupportsSpatialFilter(EMegaLightsInput InputType);

	bool UseSpatialFilter();
	bool UseTemporalFilter();

	void ModifyCompilationEnvironment(EShaderPlatform Platform, FShaderCompilerEnvironment& OutEnvironment);

	// Keep in sync with TILE_TYPE_* in shaders
	enum class ETileType : uint8
	{
		SimpleShading = TILE_MODE_SIMPLE_SHADING,
		ComplexShading = TILE_MODE_COMPLEX_SHADING,
		SimpleShading_Rect = TILE_MODE_SIMPLE_SHADING_RECT,
		ComplexShading_Rect = TILE_MODE_COMPLEX_SHADING_RECT,
		SimpleShading_Rect_Textured = TILE_MODE_SIMPLE_SHADING_RECT_TEXTURED,
		ComplexShading_Rect_Textured = TILE_MODE_COMPLEX_SHADING_RECT_TEXTURED,
		SHADING_MAX_LEGACY = TILE_MODE_EMPTY,

		Empty = TILE_MODE_EMPTY,
		MAX_LEGACY = TILE_MODE_MAX_LEGACY,

		SHADING_MIN_SUBSTRATE = TILE_MODE_SINGLE_SHADING,
		SingleShading = TILE_MODE_SINGLE_SHADING,
		ComplexSpecialShading = TILE_MODE_COMPLEX_SPECIAL_SHADING,
		SingleShading_Rect = TILE_MODE_SINGLE_SHADING_RECT,
		ComplexSpecialShading_Rect = TILE_MODE_COMPLEX_SPECIAL_SHADING_RECT,
		SingleShading_Rect_Textured = TILE_MODE_SINGLE_SHADING_RECT_TEXTURED,
		ComplexSpecialShading_Rect_Textured = TILE_MODE_COMPLEX_SPECIAL_SHADING_RECT_TEXTURED,
		SHADING_MAX_SUBSTRATE = TILE_MODE_MAX,

		MAX_SUBSTRATE = TILE_MODE_MAX
	};

	bool IsRectLightTileType(ETileType TileType);
	bool IsTexturedLightTileType(ETileType TileType);
	bool IsComplexTileType(ETileType TileType);
	TArray<int32> GetShadingTileTypes(EMegaLightsInput InputType);
	const TCHAR* GetTileTypeString(ETileType TileType);
};

namespace MegaLightsVolume
{
	int32 GetDebugMode();
	bool UsesLightFunction();
};

namespace MegaLightsTranslucencyVolume
{
	int32 GetDebugMode();
	bool UsesLightFunction();
};

BEGIN_SHADER_PARAMETER_STRUCT(FMegaLightsVolumeData, )
	SHADER_PARAMETER(FIntVector, ViewGridSizeInt)
	SHADER_PARAMETER(FVector3f, ViewGridSize)
	SHADER_PARAMETER(FIntVector, ResourceGridSizeInt)
	SHADER_PARAMETER(FVector3f, ResourceGridSize)
	SHADER_PARAMETER(FVector3f, GridZParams)
	SHADER_PARAMETER(FVector2f, SVPosToVolumeUV)
	SHADER_PARAMETER(FIntPoint, FogGridToPixelXY)
	SHADER_PARAMETER(float, MaxDistance)
END_SHADER_PARAMETER_STRUCT()

class FMegaLightsViewContext
{
public:
	FMegaLightsViewContext(
		FRDGBuilder& InGraphBuilder,
		const int32 InViewIndex,
		const FViewInfo& InView,
		const FSceneViewFamily& InViewFamily,
		const FScene* InScene,
		const FSceneTextures& InSceneTextures,
		bool bInUseVSM);

	FRDGTextureRef TileClassificationMark(uint32 ShadingPassIndex);

	void Setup(
		FRDGTextureRef LightingChannelsTexture,
		const FLumenSceneFrameTemporaries& LumenFrameTemporaries,
		const bool bInShouldRenderVolumetricFog,
		const bool bInShouldRenderTranslucencyVolume,
		TUniformBufferRef<FBlueNoise> BlueNoiseUniformBuffer,
		EMegaLightsInput InInputType);

	void GenerateSamples(
		FRDGTextureRef LightingChannelsTexture,
		uint32 ShadingPassIndex);

	void MarkVSMPages(
		const FVirtualShadowMapArray& VirtualShadowMapArray);

	void RayTrace(
		const FVirtualShadowMapArray& VirtualShadowMapArray,
		const TArrayView<FRDGTextureRef> NaniteShadingMasks,
		uint32 ShadingPassIndex);

	void Resolve(
		FRDGTextureRef OutputColorTarget,
		FMegaLightsVolume* MegaLightsVolume,
		uint32 ShadingPassIndex);

	void DenoiseLighting(FRDGTextureRef OutputColorTarget);

	bool AreSamplesGenerated()
	{
		return bSamplesGenerated;
	}

	uint32 GetReferenceShadingPassCount() const { return ReferenceShadingPassCount; }

private:
	FRDGBuilder& GraphBuilder;
	const int32 ViewIndex;
	const FViewInfo& View;
	const FSceneViewFamily& ViewFamily;
	const FScene* Scene;
	const FSceneTextures& SceneTextures;
	const bool bUseVSM;

	bool bSamplesGenerated = false;

	EMegaLightsInput InputType;

	bool bUnifiedVolume;
	bool bVolumeEnabled;
	bool bGuideByHistory = true;
	bool bGuideAreaLightsByHistory = true;
	bool bVolumeGuideByHistory;
	bool bTranslucencyVolumeGuideByHistory;
	bool bDebug;
	bool bVolumeDebug;
	bool bTranslucencyVolumeDebug;
	bool bUseLightFunctionAtlas;
	bool bSpatial;
	bool bTemporal;
	bool bSubPixelShading;
	bool bShouldRenderVolumetricFog;
	bool bShouldRenderTranslucencyVolume;

	int32 DebugTileClassificationMode = 0;
	int32 VisualizeLightLoopIterationsMode = 0;

	FMegaLightsParameters MegaLightsParameters;
	FMegaLightsVolumeParameters MegaLightsVolumeParameters;
	FMegaLightsVolumeParameters MegaLightsTranslucencyVolumeParameters;

	FMegaLightsVolumeData VolumeParameters;
	FVolumetricFogGlobalData VolumetricFogParamaters;

	FIntPoint DownsampleFactor;
	FIntPoint SampleBufferSize;
	FIntPoint DonwnsampledSampleBufferSize;

	FIntPoint NumSamplesPerPixel2d;
	FIntVector NumSamplesPerVoxel3d;
	FIntVector NumSamplesPerTranslucencyVoxel3d;

	FIntPoint ViewSizeInTiles;

	uint32 VisibleLightHashBufferSize;
	FIntPoint VisibleLightHashSizeInTiles;
	FIntPoint VisibleLightHashViewMinInTiles;
	FIntPoint VisibleLightHashViewSizeInTiles;

	uint32 VolumeDownsampleFactor;
	FIntVector VolumeBufferSize;
	FIntVector VolumeSampleBufferSize;
	FIntVector VolumeViewSize;

	FRDGTextureRef VolumeLightSamples = nullptr;
	FRDGTextureRef VolumeLightSampleRays = nullptr;

	uint32 VolumeVisibleLightHashBufferSize;
	FIntVector VolumeVisibleLightHashTileSize;
	FIntVector VolumeVisibleLightHashSizeInTiles;
	FIntVector VolumeVisibleLightHashViewSizeInTiles;
	FIntVector VolumeDownsampledViewSize;

	uint32 TranslucencyVolumeDownsampleFactor;
	FIntVector TranslucencyVolumeBufferSize;
	FIntVector TranslucencyVolumeSampleBufferSize;
	FIntVector TranslucencyVolumeDownsampledBufferSize;

	TArray<FRDGTextureRef, TInlineAllocator<TVC_MAX>> TranslucencyVolumeLightSamples;
	TArray<FRDGTextureRef, TInlineAllocator<TVC_MAX>> TranslucencyVolumeLightSampleRays;

	uint32 TranslucencyVolumeVisibleLightHashBufferSize;
	FIntVector TranslucencyVolumeVisibleLightHashTileSize;
	FIntVector TranslucencyVolumeVisibleLightHashSizeInTiles;

	FRDGTextureRef SceneDepth = nullptr;
	FRDGTextureRef SceneWorldNormal = nullptr;
	FRDGTextureRef DownsampledSceneDepth = nullptr;
	FRDGTextureRef DownsampledSceneWorldNormal = nullptr;

	FRDGBufferRef TileIndirectArgs = nullptr;
	FRDGBufferRef TileAllocator = nullptr;
	FRDGBufferRef TileData = nullptr;
	FRDGBufferRef DownsampledTileIndirectArgs = nullptr;
	FRDGBufferRef DownsampledTileAllocator = nullptr;
	FRDGBufferRef DownsampledTileData = nullptr;

	FRDGTextureRef LightSamples = nullptr;
	FRDGTextureRef LightSampleRays = nullptr;

	TArray<int32> ShadingTileTypes;

	StochasticLighting::FHistoryScreenParameters HistoryScreenParameters;
	FIntPoint HistoryVisibleLightHashViewMinInTiles = 0;
	FIntPoint HistoryVisibleLightHashViewSizeInTiles = 0;
	FRDGTextureRef DiffuseLightingHistory = nullptr;
	FRDGTextureRef SpecularLightingHistory = nullptr;
	FRDGTextureRef LightingMomentsHistory = nullptr;
	FRDGTextureRef SceneDepthHistory = nullptr;
	FRDGTextureRef SceneNormalAndShadingHistory = nullptr;
	FRDGTextureRef NumFramesAccumulatedHistory = nullptr;
	FRDGBufferRef VisibleLightHashHistory = nullptr;
	FRDGBufferRef VisibleLightMaskHashHistory = nullptr;

	FIntVector HistoryVolumeVisibleLightHashViewSizeInTiles = FIntVector::ZeroValue;
	FRDGBufferRef VolumeVisibleLightHashHistory = nullptr;

	FIntVector HistoryTranslucencyVolumeVisibleLightHashSizeInTiles = FIntVector::ZeroValue;
	FRDGBufferRef TranslucencyVolumeVisibleLightHashHistory[TVC_MAX] = {};

	// State for the shading loop; much of this gets lazily created in the loop
	// This should perhaps be moved to a separate context structure in the future
	uint32 ReferenceShadingPassCount;
	bool bReferenceMode;
	uint32 FirstPassStateFrameIndex;
	EPixelFormat AccumulatedRGBLightingDataFormat;
	EPixelFormat AccumulatedRGBALightingDataFormat;
	EPixelFormat AccumulatedConfidenceDataFormat;

	FRDGTextureRef ResolvedDiffuseLighting = nullptr;
	FRDGTextureRef ResolvedSpecularLighting = nullptr;
	FRDGTextureRef ShadingConfidence = nullptr;
	FRDGTextureRef VolumeResolvedLighting = nullptr;
	FRDGBufferRef VisibleLightHash = nullptr;
	FRDGBufferRef VisibleLightMaskHash = nullptr;
	FRDGBufferRef VolumeVisibleLightHash = nullptr;
	FRDGTextureRef TranslucencyVolumeResolvedLightingAmbient[TVC_MAX] = {};
	FRDGTextureRef TranslucencyVolumeResolvedLightingDirectional[TVC_MAX] = {};
	FRDGBufferRef TranslucencyVolumeVisibleLightHash[TVC_MAX] = {};
};
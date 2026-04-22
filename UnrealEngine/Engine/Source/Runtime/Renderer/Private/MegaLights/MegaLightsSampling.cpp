// Copyright Epic Games, Inc. All Rights Reserved.

#include "MegaLights.h"
#include "MegaLightsInternal.h"
#include "RendererPrivate.h"
#include "HairStrandsInterface.h"

static TAutoConsoleVariable<float> CVarMegaLightsMinSampleClampingWeight(
	TEXT("r.MegaLights.MinSampleClampingWeight"),
	0.01f,
	TEXT("Min weight for when any sample clamping can occur (r.MegaLights.DirectionalLightSampleFraction or r.MegaLights.GuideByHistory.VisibleSampleFraction)."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<float> CVarMegaLightsDirectionalLightSampleFraction(
	TEXT("r.MegaLights.DirectionalLightSampleFraction"),
	0.5f,
	TEXT("Max fraction of samples which should be used to sample directional lights. Higher values make directional lights higher quality, but reduce quality of local lights."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<float> CVarMegaLightsGuideByHistoryLightHiddenWeight(
	TEXT("r.MegaLights.GuideByHistory.LightHiddenWeight"),
	0.1f,
	TEXT("PDF weight scale for hidden lights."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<float> CVarMegaLightsGuideByHistoryLightHiddenWeightForHistoryMiss(
	TEXT("r.MegaLights.GuideByHistory.LightHiddenWeightForHistoryMiss"),
	0.4f,
	TEXT("PDF weight scale for hidden lights for pixels without valid temporal history."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<float> CVarMegaLightsGuideByHistoryAreaLightHiddenWeight(
	TEXT("r.MegaLights.GuideByHistory.AreaLightHiddenWeight"),
	0.25f,
	TEXT("PDF weight scale for hidden parts of an area light. 1 will disable area light guiding. Lower values will improve static quality, but will cause more artifacts in motion when area light guiding is wrong."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

namespace MegaLights
{
	float GetDirectionalLightSampleRatio()
	{
		float Fraction = CVarMegaLightsDirectionalLightSampleFraction.GetValueOnRenderThread();
		if (Fraction < 1.0f)
		{
			return Fraction / (1.0f - Fraction);
		}
		else
		{
			return 0.0f;
		}
	}
}

class FGenerateLightSamplesCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FGenerateLightSamplesCS)
	SHADER_USE_PARAMETER_STRUCT(FGenerateLightSamplesCS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		RDG_BUFFER_ACCESS(IndirectArgs, ERHIAccess::IndirectArgs)
		SHADER_PARAMETER_STRUCT_INCLUDE(FMegaLightsParameters, MegaLightsParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(StochasticLighting::FHistoryScreenParameters, HistoryScreenParameters)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, RWDownsampledSceneDepth)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<UNORM float3>, RWDownsampledSceneWorldNormal)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, RWLightSamples)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, RWLightSampleRays)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, DownsampledTileAllocator)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, DownsampledTileData)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, VisibleLightHashHistory)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, VisibleLightMaskHashHistory)
		SHADER_PARAMETER(int32, bVisualizeLightLoopIterations)
		SHADER_PARAMETER(float, LightHiddenPDFWeight)
		SHADER_PARAMETER(float, LightHiddenPDFWeightForHistoryMiss)
		SHADER_PARAMETER(float, AreaLightHiddenPDFWeight)
		SHADER_PARAMETER(float, DirectionalLightSampleRatio)
		SHADER_PARAMETER(float, MinSampleClampingWeight)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, ShadowMaskBits)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, MegaLightsDepthHistory)
		SHADER_PARAMETER(FIntPoint, HistoryVisibleLightHashViewMinInTiles)
		SHADER_PARAMETER(FIntPoint, HistoryVisibleLightHashViewSizeInTiles)
	END_SHADER_PARAMETER_STRUCT()

	class FTileType : SHADER_PERMUTATION_INT("TILE_TYPE", (int32)MegaLights::ETileType::SHADING_MAX_SUBSTRATE);
	class FNumSamplesPerPixel1d : SHADER_PERMUTATION_SPARSE_INT("NUM_SAMPLES_PER_PIXEL_1D", 2, 4, 16);
	class FGuideByHistory : SHADER_PERMUTATION_BOOL("GUIDE_BY_HISTORY");
	class FInputType : SHADER_PERMUTATION_INT("INPUT_TYPE", int32(EMegaLightsInput::Count));
	class FDebugMode : SHADER_PERMUTATION_BOOL("DEBUG_MODE");
	class FReferenceMode : SHADER_PERMUTATION_BOOL("REFERENCE_MODE");
	class FHairComplexTransmittance: SHADER_PERMUTATION_BOOL("USE_HAIR_COMPLEX_TRANSMITTANCE");
	using FPermutationDomain = TShaderPermutationDomain<FTileType, FNumSamplesPerPixel1d, FGuideByHistory, FInputType, FDebugMode, FReferenceMode, FHairComplexTransmittance>;

	static int32 GetGroupSize()
	{
		return 8;
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		FPermutationDomain PermutationVector(Parameters.PermutationId);
		const EMegaLightsInput InputType = EMegaLightsInput(PermutationVector.Get<FInputType>());
		if (MegaLights::GetShadingTileTypes(InputType).Find(PermutationVector.Get<FTileType>()) == INDEX_NONE)
		{
			return false;
		}

		// Hair complex transmittance is always enabled for hair input
		if (InputType == EMegaLightsInput::HairStrands && !PermutationVector.Get<FHairComplexTransmittance>())
		{
			return false;
		}

		// Hair complex transmittance is only enabled if:
		// * If Hair plugin is enabled
		// * For Complex tiles, as hair are only part of these type of tiles
		const MegaLights::ETileType TilType = (MegaLights::ETileType)PermutationVector.Get<FTileType>();
		if (PermutationVector.Get<FHairComplexTransmittance>() && 
			(!IsHairStrandsSupported(EHairStrandsShaderType::All, Parameters.Platform) || !IsComplexTileType(TilType)))
		{
			return false;
		}

		if (PermutationVector.Get<FReferenceMode>() && !MegaLights::ShouldCompileShadersForReferenceMode(Parameters.Platform))
		{
			return false;
		}

		return MegaLights::ShouldCompileShaders(Parameters.Platform);
	}

	static EShaderPermutationPrecacheRequest ShouldPrecachePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		FPermutationDomain PermutationVector(Parameters.PermutationId);

		// precache all tile types
		EMegaLightsInput InputType = EMegaLightsInput(PermutationVector.Get<FInputType>());
		int NumSamplesPerPixel1d = PermutationVector.Get<FNumSamplesPerPixel1d>();
		const FIntPoint NumSamplesPerPixel2d = MegaLights::GetNumSamplesPerPixel2d(InputType);
		if (NumSamplesPerPixel1d != (NumSamplesPerPixel2d.X * NumSamplesPerPixel2d.Y))
		{
			return EShaderPermutationPrecacheRequest::NotUsed;
		}

		if (PermutationVector.Get<FDebugMode>())
		{
			return EShaderPermutationPrecacheRequest::NotPrecached;
		}

		if (PermutationVector.Get<FReferenceMode>())
		{
			return EShaderPermutationPrecacheRequest::NotPrecached;
		}

		return EShaderPermutationPrecacheRequest::Precached;
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		MegaLights::ModifyCompilationEnvironment(Parameters.Platform, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GetGroupSize());

		FPermutationDomain PermutationVector(Parameters.PermutationId);
		const int32 NumSamplesPerPixel1d = PermutationVector.Get<FNumSamplesPerPixel1d>();
		const FIntPoint NumSamplesPerPixel2d = MegaLights::GetNumSamplesPerPixel2d(NumSamplesPerPixel1d);
		OutEnvironment.SetDefine(TEXT("NUM_SAMPLES_PER_PIXEL_2D_X"), NumSamplesPerPixel2d.X);
		OutEnvironment.SetDefine(TEXT("NUM_SAMPLES_PER_PIXEL_2D_Y"), NumSamplesPerPixel2d.Y);

		if (IsMetalPlatform(Parameters.Platform))
		{
			OutEnvironment.SetDefine(TEXT("FORCE_DISABLE_GLINTS_AA"), 1); // SUBSTRATE_TODO Temporary, while Metal compute does not have derivatives.
		}

		OutEnvironment.CompilerFlags.Add(CFLAG_WaveOperations);
		OutEnvironment.CompilerFlags.Add(CFLAG_Wave32);
	}
};

IMPLEMENT_GLOBAL_SHADER(FGenerateLightSamplesCS, "/Engine/Private/MegaLights/MegaLightsSampling.usf", "GenerateLightSamplesCS", SF_Compute);

class FVolumeGenerateLightSamplesCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FVolumeGenerateLightSamplesCS)
	SHADER_USE_PARAMETER_STRUCT(FVolumeGenerateLightSamplesCS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FMegaLightsParameters, MegaLightsParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(FMegaLightsVolumeParameters, MegaLightsVolumeParameters)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, VolumeVisibleLightHashHistory)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<uint>, RWVolumeLightSamples)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<uint>, RWVolumeLightSampleRays)
		SHADER_PARAMETER(FIntVector, HistoryVolumeVisibleLightHashViewSizeInTiles)
		SHADER_PARAMETER(FIntVector, VolumeVisibleLightHashTileSize)
		SHADER_PARAMETER(float, LightHiddenPDFWeight)
		SHADER_PARAMETER(float, LightHiddenPDFWeightForHistoryMiss)
		SHADER_PARAMETER(float, DirectionalLightSampleRatio)
		SHADER_PARAMETER(float, MinSampleClampingWeight)
	END_SHADER_PARAMETER_STRUCT()

	class FTranslucencyLightingVolume : SHADER_PERMUTATION_BOOL("TRANSLUCENCY_LIGHTING_VOLUME");
	class FNumSamplesPerVoxel1d : SHADER_PERMUTATION_SPARSE_INT("NUM_SAMPLES_PER_VOXEL_1D", 2, 4);
	class FLightSoftFading : SHADER_PERMUTATION_BOOL("USE_LIGHT_SOFT_FADING");
	class FUseLightFunctionAtlas : SHADER_PERMUTATION_BOOL("USE_LIGHT_FUNCTION_ATLAS");
	class FGuideByHistory : SHADER_PERMUTATION_BOOL("GUIDE_BY_HISTORY");
	class FDebugMode : SHADER_PERMUTATION_BOOL("DEBUG_MODE");
	class FReferenceMode : SHADER_PERMUTATION_BOOL("REFERENCE_MODE");
	using FPermutationDomain = TShaderPermutationDomain<FTranslucencyLightingVolume, FNumSamplesPerVoxel1d, FLightSoftFading, FUseLightFunctionAtlas, FGuideByHistory, FDebugMode, FReferenceMode>;

	static int32 GetGroupSize()
	{
		return 4;
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return MegaLights::ShouldCompileShaders(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		MegaLights::ModifyCompilationEnvironment(Parameters.Platform, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GetGroupSize());

		FPermutationDomain PermutationVector(Parameters.PermutationId);
		const int32 NumSamplesPerVoxel1d = PermutationVector.Get<FNumSamplesPerVoxel1d>();
		const FIntVector NumSamplesPerVoxel3d = MegaLights::GetNumSamplesPerVoxel3d(NumSamplesPerVoxel1d);
		OutEnvironment.SetDefine(TEXT("NUM_SAMPLES_PER_VOXEL_3D_X"), NumSamplesPerVoxel3d.X);
		OutEnvironment.SetDefine(TEXT("NUM_SAMPLES_PER_VOXEL_3D_Y"), NumSamplesPerVoxel3d.Y);
		OutEnvironment.SetDefine(TEXT("NUM_SAMPLES_PER_VOXEL_3D_Z"), NumSamplesPerVoxel3d.Z);

		OutEnvironment.CompilerFlags.Add(CFLAG_WaveOperations);
		OutEnvironment.CompilerFlags.Add(CFLAG_Wave32);
		OutEnvironment.CompilerFlags.Add(CFLAG_HLSL2021);
	}

	static EShaderPermutationPrecacheRequest ShouldPrecachePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		FPermutationDomain PermutationVector(Parameters.PermutationId);

		if (PermutationVector.Get<FDebugMode>())
		{
			return EShaderPermutationPrecacheRequest::NotPrecached;
		}

		if (PermutationVector.Get<FReferenceMode>())
		{
			return EShaderPermutationPrecacheRequest::NotPrecached;
		}

		return FGlobalShader::ShouldPrecachePermutation(Parameters);
	}
};

IMPLEMENT_GLOBAL_SHADER(FVolumeGenerateLightSamplesCS, "/Engine/Private/MegaLights/MegaLightsVolumeSampling.usf", "VolumeGenerateLightSamplesCS", SF_Compute);

class FClearLightSamplesCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FClearLightSamplesCS)
	SHADER_USE_PARAMETER_STRUCT(FClearLightSamplesCS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		RDG_BUFFER_ACCESS(IndirectArgs, ERHIAccess::IndirectArgs)
		SHADER_PARAMETER_STRUCT_INCLUDE(FMegaLightsParameters, MegaLightsParameters)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, RWDownsampledSceneDepth)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<UNORM float3>, RWDownsampledSceneWorldNormal)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, RWLightSamples)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, RWLightSampleRays)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, DownsampledTileAllocator)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, DownsampledTileData)
	END_SHADER_PARAMETER_STRUCT()

	class FDebugMode : SHADER_PERMUTATION_BOOL("DEBUG_MODE");
	using FPermutationDomain = TShaderPermutationDomain<FDebugMode>;

	static int32 GetGroupSize()
	{
		return 8;
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return MegaLights::ShouldCompileShaders(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		MegaLights::ModifyCompilationEnvironment(Parameters.Platform, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GetGroupSize());
	}

	static EShaderPermutationPrecacheRequest ShouldPrecachePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		FPermutationDomain PermutationVector(Parameters.PermutationId);
		if (PermutationVector.Get<FDebugMode>())
		{
			return EShaderPermutationPrecacheRequest::NotPrecached;
		}
		return FGlobalShader::ShouldPrecachePermutation(Parameters);
	}
};

IMPLEMENT_GLOBAL_SHADER(FClearLightSamplesCS, "/Engine/Private/MegaLights/MegaLightsSampling.usf", "ClearLightSamplesCS", SF_Compute);

void FMegaLightsViewContext::GenerateSamples(
	FRDGTextureRef LightingChannelsTexture,
	uint32 ShadingPassIndex)
{
	RDG_EVENT_SCOPE_CONDITIONAL(GraphBuilder, bReferenceMode, "Pass%d", ShadingPassIndex);

	const bool bDebugPass = bDebug && MegaLights::IsDebugEnabledForShadingPass(ShadingPassIndex, View.GetShaderPlatform());
	MegaLightsParameters.MegaLightsStateFrameIndex = FirstPassStateFrameIndex + ShadingPassIndex;

	if (ShadingPassIndex > 0)
	{
		MegaLightsParameters.StochasticLightingStateFrameIndex = MegaLightsParameters.MegaLightsStateFrameIndex;
	}

	// Generate new candidate light samples
	{
		FRDGTextureUAVRef DownsampledSceneDepthUAV = GraphBuilder.CreateUAV(DownsampledSceneDepth, ERDGUnorderedAccessViewFlags::SkipBarrier);
		FRDGTextureUAVRef DownsampledSceneWorldNormalUAV = GraphBuilder.CreateUAV(DownsampledSceneWorldNormal, ERDGUnorderedAccessViewFlags::SkipBarrier);
		FRDGTextureUAVRef LightSamplesUAV = GraphBuilder.CreateUAV(LightSamples, ERDGUnorderedAccessViewFlags::SkipBarrier);
		FRDGTextureUAVRef LightSampleRaysUAV = GraphBuilder.CreateUAV(LightSampleRays, ERDGUnorderedAccessViewFlags::SkipBarrier);

		// Clear tiles which don't contain any lights or geometry
		{
			FClearLightSamplesCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FClearLightSamplesCS::FParameters>();
			PassParameters->IndirectArgs = DownsampledTileIndirectArgs;
			PassParameters->MegaLightsParameters = MegaLightsParameters;
			PassParameters->RWDownsampledSceneDepth = DownsampledSceneDepthUAV;
			PassParameters->RWDownsampledSceneWorldNormal = DownsampledSceneWorldNormalUAV;
			PassParameters->RWLightSamples = LightSamplesUAV;
			PassParameters->RWLightSampleRays = LightSampleRaysUAV;
			PassParameters->DownsampledTileAllocator = GraphBuilder.CreateSRV(DownsampledTileAllocator);
			PassParameters->DownsampledTileData = GraphBuilder.CreateSRV(DownsampledTileData);

			FClearLightSamplesCS::FPermutationDomain PermutationVector;
			PermutationVector.Set<FClearLightSamplesCS::FDebugMode>(bDebug);
			auto ComputeShader = View.ShaderMap->GetShader<FClearLightSamplesCS>(PermutationVector);

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("ClearLightSamples"),
				ComputeShader,
				PassParameters,
				DownsampledTileIndirectArgs,
				(int32)MegaLights::ETileType::Empty * sizeof(FRHIDispatchIndirectParameters));
		}

		const bool bVisualizeLightLoopIterations = VisualizeLightLoopIterationsMode == 2;
		const bool bHairComplexTransmittance = InputType == EMegaLightsInput::HairStrands || (View.HairCardsMeshElements.Num() && IsHairStrandsSupported(EHairStrandsShaderType::All, View.GetShaderPlatform()));

		for (const int32 ShadingTileType : ShadingTileTypes)
		{
			const MegaLights::ETileType TileType = (MegaLights::ETileType)ShadingTileType;
			if (!View.bLightGridHasRectLights && IsRectLightTileType(TileType))
			{
				continue;
			}

			if (!View.bLightGridHasTexturedLights && IsTexturedLightTileType(TileType))
			{
				continue;
			}

			const bool bIsComplexTile = MegaLights::IsComplexTileType(TileType);

			FGenerateLightSamplesCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FGenerateLightSamplesCS::FParameters>();
			PassParameters->IndirectArgs = DownsampledTileIndirectArgs;
			PassParameters->MegaLightsParameters = MegaLightsParameters;
			PassParameters->HistoryScreenParameters = HistoryScreenParameters;
			PassParameters->RWDownsampledSceneDepth = DownsampledSceneDepthUAV;
			PassParameters->RWDownsampledSceneWorldNormal = DownsampledSceneWorldNormalUAV;
			PassParameters->RWLightSamples = LightSamplesUAV;
			PassParameters->RWLightSampleRays = LightSampleRaysUAV;
			PassParameters->DownsampledTileAllocator = GraphBuilder.CreateSRV(DownsampledTileAllocator);
			PassParameters->DownsampledTileData = GraphBuilder.CreateSRV(DownsampledTileData);
			PassParameters->VisibleLightHashHistory = VisibleLightHashHistory != nullptr ? GraphBuilder.CreateSRV(VisibleLightHashHistory) : nullptr;
			PassParameters->VisibleLightMaskHashHistory = VisibleLightMaskHashHistory != nullptr ? GraphBuilder.CreateSRV(VisibleLightMaskHashHistory) : nullptr;
			PassParameters->bVisualizeLightLoopIterations = bVisualizeLightLoopIterations;
			PassParameters->LightHiddenPDFWeight = CVarMegaLightsGuideByHistoryLightHiddenWeight.GetValueOnRenderThread();
			PassParameters->LightHiddenPDFWeightForHistoryMiss = CVarMegaLightsGuideByHistoryLightHiddenWeightForHistoryMiss.GetValueOnRenderThread();
			PassParameters->AreaLightHiddenPDFWeight = bGuideAreaLightsByHistory ? CVarMegaLightsGuideByHistoryAreaLightHiddenWeight.GetValueOnRenderThread() : 1.0f;
			PassParameters->DirectionalLightSampleRatio = MegaLights::GetDirectionalLightSampleRatio();
			PassParameters->MinSampleClampingWeight = CVarMegaLightsMinSampleClampingWeight.GetValueOnRenderThread();
			PassParameters->MegaLightsDepthHistory = SceneDepthHistory;
			PassParameters->HistoryVisibleLightHashViewMinInTiles = HistoryVisibleLightHashViewMinInTiles;
			PassParameters->HistoryVisibleLightHashViewSizeInTiles = HistoryVisibleLightHashViewSizeInTiles;

			FGenerateLightSamplesCS::FPermutationDomain PermutationVector;
			PermutationVector.Set<FGenerateLightSamplesCS::FTileType>(ShadingTileType);
			PermutationVector.Set<FGenerateLightSamplesCS::FNumSamplesPerPixel1d>(NumSamplesPerPixel2d.X * NumSamplesPerPixel2d.Y);
			PermutationVector.Set<FGenerateLightSamplesCS::FGuideByHistory>(VisibleLightHashHistory != nullptr && SceneDepthHistory != nullptr);
			PermutationVector.Set<FGenerateLightSamplesCS::FInputType>(uint32(InputType));
			PermutationVector.Set<FGenerateLightSamplesCS::FDebugMode>(bDebugPass || bVisualizeLightLoopIterations);
			PermutationVector.Set<FGenerateLightSamplesCS::FReferenceMode>(bReferenceMode);
			PermutationVector.Set<FGenerateLightSamplesCS::FHairComplexTransmittance>(bHairComplexTransmittance && bIsComplexTile);
			auto ComputeShader = View.ShaderMap->GetShader<FGenerateLightSamplesCS>(PermutationVector);

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("GenerateSamples DownsampleFactor:%dx%d SamplesPerPixel:%dx%d TileType:%s",
					DownsampleFactor.X, DownsampleFactor.Y, NumSamplesPerPixel2d.X, NumSamplesPerPixel2d.Y, MegaLights::GetTileTypeString(TileType)),
				ComputeShader,
				PassParameters,
				DownsampledTileIndirectArgs,
				ShadingTileType * sizeof(FRHIDispatchIndirectParameters));
		}
	}

	if (bVolumeEnabled)
	{
		VolumeLightSamples = GraphBuilder.CreateTexture(
			FRDGTextureDesc::Create3D(VolumeSampleBufferSize, PF_R32_UINT, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV | TexCreate_3DTiling),
			TEXT("MegaLights.Volume.LightSamples"));

		VolumeLightSampleRays = GraphBuilder.CreateTexture(
			FRDGTextureDesc::Create3D(VolumeSampleBufferSize, PF_R8_UINT, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV | TexCreate_3DTiling),
			TEXT("MegaLights.Volume.LightSampleRays"));

		// Generate new candidate light samples for the volume
		{
			FVolumeGenerateLightSamplesCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FVolumeGenerateLightSamplesCS::FParameters>();
			PassParameters->MegaLightsParameters = MegaLightsParameters;
			PassParameters->MegaLightsVolumeParameters = MegaLightsVolumeParameters;
			PassParameters->VolumeVisibleLightHashHistory = VolumeVisibleLightHashHistory != nullptr ? GraphBuilder.CreateSRV(VolumeVisibleLightHashHistory) : nullptr;
			PassParameters->RWVolumeLightSamples = GraphBuilder.CreateUAV(VolumeLightSamples);
			PassParameters->RWVolumeLightSampleRays = GraphBuilder.CreateUAV(VolumeLightSampleRays);
			PassParameters->HistoryVolumeVisibleLightHashViewSizeInTiles = HistoryVolumeVisibleLightHashViewSizeInTiles;
			PassParameters->VolumeVisibleLightHashTileSize = VolumeVisibleLightHashTileSize;
			PassParameters->LightHiddenPDFWeight = CVarMegaLightsGuideByHistoryLightHiddenWeight.GetValueOnRenderThread();
			PassParameters->LightHiddenPDFWeightForHistoryMiss = CVarMegaLightsGuideByHistoryLightHiddenWeightForHistoryMiss.GetValueOnRenderThread();
			PassParameters->DirectionalLightSampleRatio = MegaLights::GetDirectionalLightSampleRatio();
			PassParameters->MinSampleClampingWeight = CVarMegaLightsMinSampleClampingWeight.GetValueOnRenderThread();

			FVolumeGenerateLightSamplesCS::FPermutationDomain PermutationVector;
			PermutationVector.Set<FVolumeGenerateLightSamplesCS::FTranslucencyLightingVolume>(false);
			PermutationVector.Set<FVolumeGenerateLightSamplesCS::FNumSamplesPerVoxel1d>(NumSamplesPerVoxel3d.X * NumSamplesPerVoxel3d.Y * NumSamplesPerVoxel3d.Z);
			PermutationVector.Set<FVolumeGenerateLightSamplesCS::FLightSoftFading>(MegaLightsVolumeParameters.LightSoftFading > 0.0f);
			PermutationVector.Set<FVolumeGenerateLightSamplesCS::FUseLightFunctionAtlas>(bUseLightFunctionAtlas && MegaLightsVolume::UsesLightFunction());
			PermutationVector.Set<FVolumeGenerateLightSamplesCS::FGuideByHistory>(VolumeVisibleLightHashHistory != nullptr);
			PermutationVector.Set<FVolumeGenerateLightSamplesCS::FDebugMode>(bVolumeDebug);
			PermutationVector.Set<FVolumeGenerateLightSamplesCS::FReferenceMode>(bReferenceMode);

			auto ComputeShader = View.ShaderMap->GetShader<FVolumeGenerateLightSamplesCS>(PermutationVector);

			const FIntVector GroupCount = FComputeShaderUtils::GetGroupCount(VolumeDownsampledViewSize, FVolumeGenerateLightSamplesCS::GetGroupSize());

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("VolumeGenerateSamples SamplesPerVoxel:%dx%dx%d", NumSamplesPerVoxel3d.X, NumSamplesPerVoxel3d.Y, NumSamplesPerVoxel3d.Z),
				ComputeShader,
				PassParameters,
				GroupCount);
		}
	}

	if (MegaLights::UseTranslucencyVolume() && bShouldRenderTranslucencyVolume && !bUnifiedVolume)
	{
		TranslucencyVolumeLightSamples.AddDefaulted(TVC_MAX);
		TranslucencyVolumeLightSampleRays.AddDefaulted(TVC_MAX);

		for (uint32 CascadeIndex = 0; CascadeIndex < TVC_MAX; ++CascadeIndex)
		{
			TranslucencyVolumeLightSamples[CascadeIndex] = GraphBuilder.CreateTexture(
				FRDGTextureDesc::Create3D(TranslucencyVolumeSampleBufferSize, PF_R32_UINT, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV | TexCreate_3DTiling),
				TEXT("MegaLights.TranslucencyVolume.LightSamples"));

			TranslucencyVolumeLightSampleRays[CascadeIndex] = GraphBuilder.CreateTexture(
				FRDGTextureDesc::Create3D(TranslucencyVolumeSampleBufferSize, PF_R8_UINT, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV | TexCreate_3DTiling),
				TEXT("MegaLights.TranslucencyVolume.LightSampleRays"));

			// Generate new candidate light samples for the Translucency Volume
			{
				FVolumeGenerateLightSamplesCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FVolumeGenerateLightSamplesCS::FParameters>();
				PassParameters->MegaLightsParameters = MegaLightsParameters;
				PassParameters->MegaLightsVolumeParameters = MegaLightsTranslucencyVolumeParameters;
				PassParameters->MegaLightsVolumeParameters.TranslucencyVolumeCascadeIndex = CascadeIndex;
				PassParameters->VolumeVisibleLightHashHistory = TranslucencyVolumeVisibleLightHashHistory[CascadeIndex] != nullptr ? GraphBuilder.CreateSRV(TranslucencyVolumeVisibleLightHashHistory[CascadeIndex]) : nullptr;
				PassParameters->RWVolumeLightSamples = GraphBuilder.CreateUAV(TranslucencyVolumeLightSamples[CascadeIndex]);
				PassParameters->RWVolumeLightSampleRays = GraphBuilder.CreateUAV(TranslucencyVolumeLightSampleRays[CascadeIndex]);
				PassParameters->HistoryVolumeVisibleLightHashViewSizeInTiles = HistoryTranslucencyVolumeVisibleLightHashSizeInTiles;
				PassParameters->VolumeVisibleLightHashTileSize = TranslucencyVolumeVisibleLightHashTileSize;
				PassParameters->DirectionalLightSampleRatio = MegaLights::GetDirectionalLightSampleRatio();
				PassParameters->MinSampleClampingWeight = CVarMegaLightsMinSampleClampingWeight.GetValueOnRenderThread();

				FVolumeGenerateLightSamplesCS::FPermutationDomain PermutationVector;
				PermutationVector.Set<FVolumeGenerateLightSamplesCS::FTranslucencyLightingVolume>(true);
				PermutationVector.Set<FVolumeGenerateLightSamplesCS::FNumSamplesPerVoxel1d>(NumSamplesPerTranslucencyVoxel3d.X * NumSamplesPerTranslucencyVoxel3d.Y * NumSamplesPerTranslucencyVoxel3d.Z);
				PermutationVector.Set<FVolumeGenerateLightSamplesCS::FLightSoftFading >(false);
				PermutationVector.Set<FVolumeGenerateLightSamplesCS::FUseLightFunctionAtlas>(bUseLightFunctionAtlas && MegaLightsTranslucencyVolume::UsesLightFunction());
				PermutationVector.Set<FVolumeGenerateLightSamplesCS::FGuideByHistory>(TranslucencyVolumeVisibleLightHashHistory[CascadeIndex] != nullptr);
				PermutationVector.Set<FVolumeGenerateLightSamplesCS::FDebugMode>(bTranslucencyVolumeDebug);
				PermutationVector.Set<FVolumeGenerateLightSamplesCS::FReferenceMode>(bReferenceMode);
				auto ComputeShader = View.ShaderMap->GetShader<FVolumeGenerateLightSamplesCS>(PermutationVector);

				const FIntVector GroupCount = FComputeShaderUtils::GetGroupCount(TranslucencyVolumeDownsampledBufferSize, FVolumeGenerateLightSamplesCS::GetGroupSize());

				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME("TranslucencyVolumeGenerateSamples SamplesPerVoxel:%dx%dx%d", NumSamplesPerTranslucencyVoxel3d.X, NumSamplesPerTranslucencyVoxel3d.Y, NumSamplesPerTranslucencyVoxel3d.Z),
					ComputeShader,
					PassParameters,
					GroupCount);
			}
		}
	}

	bSamplesGenerated = true;
}
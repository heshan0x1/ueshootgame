// Copyright Epic Games, Inc. All Rights Reserved.

#include "MegaLights.h"
#include "MegaLightsInternal.h"
#include "RendererPrivate.h"
#include "Quantization.h"

static TAutoConsoleVariable<bool> CVarMegaLightsTemporal(
	TEXT("r.MegaLights.Temporal"),
	true,
	TEXT("Whether to use temporal accumulation for shadow mask."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarMegaLightsTemporalMinFramesAccumulatedForHistoryMiss(
	TEXT("r.MegaLights.Temporal.MinFramesAccumulatedForHistoryMiss"),
	4,
	TEXT("Minimal amount of history length when reducing history length due to a history miss. Higher values than 1 soften and slowdown transitions."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarMegaLightsTemporalMinFramesAccumulatedForHighConfidence(
	TEXT("r.MegaLights.Temporal.MinFramesAccumulatedForHighConfidence"),
	2,
	TEXT("Minimal amount of history length when reducing history length due to a high confidence. Higher values than 1 soften image, but reduce noise in high confidence areas."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarMegaLightsTemporalMaxFramesAccumulated(
	TEXT("r.MegaLights.Temporal.MaxFramesAccumulated"),
	12,
	TEXT("Max history length when accumulating frames. Lower values have less ghosting, but more noise."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<float> CVarMegaLightsTemporalNeighborhoodClampScale(
	TEXT("r.MegaLights.Temporal.NeighborhoodClampScale"),
	1.0f,
	TEXT("Scales how permissive is neighborhood clamp. Higher values increase ghosting, but reduce noise and instability."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<bool> CVarMegaLightsSpatial(
	TEXT("r.MegaLights.Spatial"),
	true,
	TEXT("Whether denoiser should run spatial filter."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<float> CVarMegaLightsSpatialDepthWeightScale(
	TEXT("r.MegaLights.Spatial.DepthWeightScale"),
	10000.0f,
	TEXT("Scales the depth weight of the spatial filter. Smaller values allow for more sample reuse, but also introduce more bluriness between unrelated surfaces."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<float> CVarMegaLightsSpatialKernelRadius(
	TEXT("r.MegaLights.Spatial.KernelRadius"),
	8.0f,
	TEXT("Spatial filter kernel radius in pixels"),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarMegaLightsSpatialNumSamples(
	TEXT("r.MegaLights.Spatial.NumSamples"),
	4,
	TEXT("Number of spatial filter samples."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarMegaLightsSpatialMaxDisocclusionFrames(
	TEXT("r.MegaLights.Spatial.MaxDisocclusionFrames"),
	3,
	TEXT("Number of of history frames to boost spatial filtering in order to minimize noise after disocclusion."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

namespace MegaLights
{
	bool UseSpatialFilter()
	{
		return CVarMegaLightsSpatial.GetValueOnRenderThread();
	}

	bool UseTemporalFilter()
	{
		return CVarMegaLightsTemporal.GetValueOnRenderThread();
	}

	float GetTemporalMaxFramesAccumulated()
	{
		return FMath::Max(CVarMegaLightsTemporalMaxFramesAccumulated.GetValueOnRenderThread(), 1.0f);
	}

	float GetSpatialFilterMaxDisocclusionFrames()
	{
		return FMath::Max(FMath::Min(CVarMegaLightsSpatialMaxDisocclusionFrames.GetValueOnRenderThread(), GetTemporalMaxFramesAccumulated() - 1.0f), 0.0f);
	}
}

class FDenoiserTemporalCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FDenoiserTemporalCS)
	SHADER_USE_PARAMETER_STRUCT(FDenoiserTemporalCS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FMegaLightsParameters, MegaLightsParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(StochasticLighting::FHistoryScreenParameters, HistoryScreenParameters)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, ResolvedDiffuseLighting)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, ResolvedSpecularLighting)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, ShadingConfidenceTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float3>, DiffuseLightingHistoryTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float3>, SpecularLightingHistoryTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, LightingMomentsHistoryTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<UNORM float>, NumFramesAccumulatedHistoryTexture)
		SHADER_PARAMETER(FVector3f, TargetFormatQuantizationError)
		SHADER_PARAMETER(float, PrevSceneColorPreExposureCorrection)
		SHADER_PARAMETER(float, TemporalMaxFramesAccumulated)
		SHADER_PARAMETER(float, TemporalNeighborhoodClampScale)
		SHADER_PARAMETER(float, MinFramesAccumulatedForHistoryMiss)
		SHADER_PARAMETER(float, MinFramesAccumulatedForHighConfidence)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float3>, RWDiffuseLighting)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float3>, RWSpecularLighting)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, RWLightingMoments)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<UNORM float>, RWNumFramesAccumulated)
	END_SHADER_PARAMETER_STRUCT()

	class FDownsampleFactorX : SHADER_PERMUTATION_RANGE_INT("DOWNSAMPLE_FACTOR_X", 1, 2);
	class FDownsampleFactorY : SHADER_PERMUTATION_RANGE_INT("DOWNSAMPLE_FACTOR_Y", 1, 2);
	class FValidHistory : SHADER_PERMUTATION_BOOL("VALID_HISTORY");
	class FDebugMode : SHADER_PERMUTATION_BOOL("DEBUG_MODE");
	using FPermutationDomain = TShaderPermutationDomain<FDownsampleFactorX, FDownsampleFactorY, FValidHistory, FDebugMode>;

	static int32 GetGroupSize()
	{
		return 8;
	}

	static FPermutationDomain RemapPermutation(FPermutationDomain PermutationVector)
	{
		if (PermutationVector.Get<FDownsampleFactorY>() == 2)
		{
			PermutationVector.Set<FDownsampleFactorX>(2);
		}

		return PermutationVector;
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		FPermutationDomain PermutationVector(Parameters.PermutationId);
		if (RemapPermutation(PermutationVector) != PermutationVector)
		{
			return false;
		}

		return MegaLights::ShouldCompileShaders(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GetGroupSize());

		if (FDataDrivenShaderPlatformInfo::GetSupportsRealTypes(Parameters.Platform) == ERHIFeatureSupport::RuntimeGuaranteed)
		{
			OutEnvironment.CompilerFlags.Add(CFLAG_AllowRealTypes);
		}
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

IMPLEMENT_GLOBAL_SHADER(FDenoiserTemporalCS, "/Engine/Private/MegaLights/MegaLightsDenoiserTemporal.usf", "DenoiserTemporalCS", SF_Compute);

class FDenoiserSpatialCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FDenoiserSpatialCS)
	SHADER_USE_PARAMETER_STRUCT(FDenoiserSpatialCS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FMegaLightsParameters, MegaLightsParameters)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, RWSceneColor)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, DiffuseLightingTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, SpecularLightingTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, LightingMomentsTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, ShadingConfidenceTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<UNORM float>, NumFramesAccumulatedTexture)
		SHADER_PARAMETER(float, TemporalMaxFramesAccumulated)
		SHADER_PARAMETER(float, SpatialFilterDepthWeightScale)
		SHADER_PARAMETER(float, SpatialFilterKernelRadius)
		SHADER_PARAMETER(uint32, SpatialFilterNumSamples)
		SHADER_PARAMETER(float, SpatialFilterMaxDisocclusionFrames)
		SHADER_PARAMETER(uint32, bSubPixelShading)
	END_SHADER_PARAMETER_STRUCT()

	class FSpatialFilter : SHADER_PERMUTATION_BOOL("SPATIAL_FILTER");
	class FInputType : SHADER_PERMUTATION_INT("INPUT_TYPE", int32(EMegaLightsInput::Count));
	class FDebugMode : SHADER_PERMUTATION_BOOL("DEBUG_MODE");
	using FPermutationDomain = TShaderPermutationDomain<FSpatialFilter, FInputType, FDebugMode>;

	static int32 GetGroupSize()
	{
		return 8;
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		FPermutationDomain PermutationVector(Parameters.PermutationId);
		EMegaLightsInput InputType = EMegaLightsInput(PermutationVector.Get<FInputType>());
		if (PermutationVector.Get<FSpatialFilter>() && !MegaLights::SupportsSpatialFilter(InputType))
		{
			return false;
		}
		return MegaLights::ShouldCompileShaders(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
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

IMPLEMENT_GLOBAL_SHADER(FDenoiserSpatialCS, "/Engine/Private/MegaLights/MegaLightsDenoiserSpatial.usf", "DenoiserSpatialCS", SF_Compute);

class FMegaLightsDebugCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FMegaLightsDebugCS)
	SHADER_USE_PARAMETER_STRUCT(FMegaLightsDebugCS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		RDG_BUFFER_ACCESS(IndirectArgs, ERHIAccess::IndirectArgs)
		SHADER_PARAMETER_STRUCT_INCLUDE(FMegaLightsParameters, MegaLightsParameters)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, TileAllocator)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, TileData)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, DownsampledTileAllocator)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, DownsampledTileData)
		SHADER_PARAMETER(uint32, DebugTileClassificationMode)
	END_SHADER_PARAMETER_STRUCT()

	static int32 GetGroupSize()
	{
		return 64;
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return MegaLights::ShouldCompileShaders(Parameters.Platform);
	}

	static EShaderPermutationPrecacheRequest ShouldPrecachePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return EShaderPermutationPrecacheRequest::NotPrecached;
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		MegaLights::ModifyCompilationEnvironment(Parameters.Platform, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GetGroupSize());
	}
};

IMPLEMENT_GLOBAL_SHADER(FMegaLightsDebugCS, "/Engine/Private/MegaLights/MegaLightsDebug.usf", "MegaLightsDebugCS", SF_Compute);

void FMegaLightsViewContext::DenoiseLighting(FRDGTextureRef OutputColorTarget)
{
	// Demodulated lighting components with second luminance moments stored in alpha channel for temporal variance tracking
	// This will be passed to the next frame
	const EPixelFormat LightingDataFormat = MegaLights::GetLightingDataFormat();
	FRDGTextureRef DiffuseLighting = nullptr;
	FRDGTextureRef SpecularLighting = nullptr;
	FRDGTextureRef LightingMoments = nullptr;

	DiffuseLighting = GraphBuilder.CreateTexture(
		FRDGTextureDesc::Create2D(View.GetSceneTexturesConfig().Extent, LightingDataFormat, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV),
		TEXT("MegaLights.DiffuseLighting"));

	SpecularLighting = GraphBuilder.CreateTexture(
		FRDGTextureDesc::Create2D(View.GetSceneTexturesConfig().Extent, LightingDataFormat, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV),
		TEXT("MegaLights.SpecularLighting"));

	LightingMoments = GraphBuilder.CreateTexture(
		FRDGTextureDesc::Create2D(View.GetSceneTexturesConfig().Extent, PF_FloatRGBA, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV),
		TEXT("MegaLights.LightingMoments"));

	FRDGTextureRef NumFramesAccumulated = GraphBuilder.CreateTexture(
		FRDGTextureDesc::Create2D(View.GetSceneTexturesConfig().Extent, PF_G8, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV),
		TEXT("MegaLights.NumFramesAccumulated"));

	// Temporal accumulation
	{
		const bool bValidHistory = DiffuseLightingHistory && SceneDepthHistory && SceneNormalAndShadingHistory && bTemporal;

		FDenoiserTemporalCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FDenoiserTemporalCS::FParameters>();
		PassParameters->MegaLightsParameters = MegaLightsParameters;
		PassParameters->HistoryScreenParameters = HistoryScreenParameters;
		PassParameters->ResolvedDiffuseLighting = ResolvedDiffuseLighting;
		PassParameters->ResolvedSpecularLighting = ResolvedSpecularLighting;
		PassParameters->ShadingConfidenceTexture = ShadingConfidence;
		PassParameters->DiffuseLightingHistoryTexture = DiffuseLightingHistory;
		PassParameters->SpecularLightingHistoryTexture = SpecularLightingHistory;
		PassParameters->LightingMomentsHistoryTexture = LightingMomentsHistory;
		PassParameters->NumFramesAccumulatedHistoryTexture = NumFramesAccumulatedHistory;
		PassParameters->TargetFormatQuantizationError = ComputePixelFormatQuantizationError(LightingDataFormat);
		PassParameters->PrevSceneColorPreExposureCorrection = View.PreExposure / View.PrevViewInfo.SceneColorPreExposure;
		PassParameters->TemporalMaxFramesAccumulated = MegaLights::GetTemporalMaxFramesAccumulated();
		PassParameters->TemporalNeighborhoodClampScale = CVarMegaLightsTemporalNeighborhoodClampScale.GetValueOnRenderThread();
		PassParameters->MinFramesAccumulatedForHistoryMiss = FMath::Clamp(CVarMegaLightsTemporalMinFramesAccumulatedForHistoryMiss.GetValueOnRenderThread(), 1.0f, MegaLights::GetTemporalMaxFramesAccumulated());
		PassParameters->MinFramesAccumulatedForHighConfidence = FMath::Clamp(CVarMegaLightsTemporalMinFramesAccumulatedForHighConfidence.GetValueOnRenderThread(), 1.0f, MegaLights::GetTemporalMaxFramesAccumulated());
		PassParameters->RWDiffuseLighting = GraphBuilder.CreateUAV(DiffuseLighting);
		PassParameters->RWSpecularLighting = GraphBuilder.CreateUAV(SpecularLighting);
		PassParameters->RWLightingMoments = GraphBuilder.CreateUAV(LightingMoments);
		PassParameters->RWNumFramesAccumulated = GraphBuilder.CreateUAV(NumFramesAccumulated);

		FDenoiserTemporalCS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FDenoiserTemporalCS::FDownsampleFactorX>(DownsampleFactor.X);
		PermutationVector.Set<FDenoiserTemporalCS::FDownsampleFactorY>(DownsampleFactor.Y);
		PermutationVector.Set<FDenoiserTemporalCS::FValidHistory>(bValidHistory);
		PermutationVector.Set<FDenoiserTemporalCS::FDebugMode>(bDebug);
		PermutationVector = FDenoiserTemporalCS::RemapPermutation(PermutationVector);

		auto ComputeShader = View.ShaderMap->GetShader<FDenoiserTemporalCS>(PermutationVector);

		const FIntVector GroupCount = FComputeShaderUtils::GetGroupCount(View.ViewRect.Size(), FDenoiserTemporalCS::GetGroupSize());

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("TemporalAccumulation"),
			ComputeShader,
			PassParameters,
			GroupCount);
	}

	// Spatial filter
	{
		FDenoiserSpatialCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FDenoiserSpatialCS::FParameters>();
		PassParameters->MegaLightsParameters = MegaLightsParameters;
		PassParameters->RWSceneColor = GraphBuilder.CreateUAV(OutputColorTarget);
		PassParameters->DiffuseLightingTexture = DiffuseLighting;
		PassParameters->SpecularLightingTexture = SpecularLighting;
		PassParameters->LightingMomentsTexture = LightingMoments;
		PassParameters->ShadingConfidenceTexture = ShadingConfidence;
		PassParameters->NumFramesAccumulatedTexture = NumFramesAccumulated;
		PassParameters->TemporalMaxFramesAccumulated = MegaLights::GetTemporalMaxFramesAccumulated();
		PassParameters->SpatialFilterDepthWeightScale = CVarMegaLightsSpatialDepthWeightScale.GetValueOnRenderThread();
		PassParameters->SpatialFilterKernelRadius = CVarMegaLightsSpatialKernelRadius.GetValueOnRenderThread();
		PassParameters->SpatialFilterNumSamples = FMath::Clamp(CVarMegaLightsSpatialNumSamples.GetValueOnRenderThread(), 0, 1024);
		PassParameters->SpatialFilterMaxDisocclusionFrames = MegaLights::GetSpatialFilterMaxDisocclusionFrames();
		PassParameters->bSubPixelShading = bSubPixelShading ? 1u : 0u;

		FDenoiserSpatialCS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FDenoiserSpatialCS::FSpatialFilter>(bSpatial);
		PermutationVector.Set<FDenoiserSpatialCS::FInputType>(uint32(InputType));
		PermutationVector.Set<FDenoiserSpatialCS::FDebugMode>(bDebug);
		auto ComputeShader = View.ShaderMap->GetShader<FDenoiserSpatialCS>(PermutationVector);

		const FIntVector GroupCount = FComputeShaderUtils::GetGroupCount(View.ViewRect.Size(), FDenoiserSpatialCS::GetGroupSize());

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("Spatial"),
			ComputeShader,
			PassParameters,
			GroupCount);
	}

	// Debug pass
	if (DebugTileClassificationMode != 0 && ((DebugTileClassificationMode - 1) / 2) == (uint32)InputType)
	{
		FMegaLightsDebugCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FMegaLightsDebugCS::FParameters>();
		PassParameters->IndirectArgs = DownsampledTileIndirectArgs;
		PassParameters->MegaLightsParameters = MegaLightsParameters;
		PassParameters->TileAllocator = GraphBuilder.CreateSRV(TileAllocator);
		PassParameters->TileData = GraphBuilder.CreateSRV(TileData);
		PassParameters->DownsampledTileAllocator = GraphBuilder.CreateSRV(DownsampledTileAllocator);
		PassParameters->DownsampledTileData = GraphBuilder.CreateSRV(DownsampledTileData);
		PassParameters->DebugTileClassificationMode = DebugTileClassificationMode;

		auto ComputeShader = View.ShaderMap->GetShader<FMegaLightsDebugCS>();
		const FIntVector GroupCount = FComputeShaderUtils::GetGroupCount(ViewSizeInTiles.X * ViewSizeInTiles.Y, FMegaLightsDebugCS::GetGroupSize());

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("Debug"),
			ComputeShader,
			PassParameters,
			GroupCount);
	}

	if (View.ViewState && !View.bStatePrevViewInfoIsReadOnly)
	{
		FMegaLightsViewState::FResources& MegaLightsViewState = InputType == EMegaLightsInput::HairStrands ? View.ViewState->MegaLights.HairStrands : View.ViewState->MegaLights.GBuffer;

		MegaLightsViewState.HistoryVisibleLightHashViewMinInTiles = VisibleLightHashViewMinInTiles;
		MegaLightsViewState.HistoryVisibleLightHashViewSizeInTiles = VisibleLightHashViewSizeInTiles;

		MegaLightsViewState.HistoryVolumeVisibleLightHashViewSizeInTiles = VolumeVisibleLightHashViewSizeInTiles;
		MegaLightsViewState.HistoryTranslucencyVolumeVisibleLightHashSizeInTiles = TranslucencyVolumeVisibleLightHashSizeInTiles;

		if (DiffuseLighting && SpecularLighting && LightingMoments && NumFramesAccumulated && bTemporal)
		{
			GraphBuilder.QueueTextureExtraction(DiffuseLighting, &MegaLightsViewState.DiffuseLightingHistory);
			GraphBuilder.QueueTextureExtraction(SpecularLighting, &MegaLightsViewState.SpecularLightingHistory);
			GraphBuilder.QueueTextureExtraction(LightingMoments, &MegaLightsViewState.LightingMomentsHistory);
			GraphBuilder.QueueTextureExtraction(NumFramesAccumulated, &MegaLightsViewState.NumFramesAccumulatedHistory);
		}
		else
		{
			MegaLightsViewState.DiffuseLightingHistory = nullptr;
			MegaLightsViewState.SpecularLightingHistory = nullptr;
			MegaLightsViewState.LightingMomentsHistory = nullptr;
			MegaLightsViewState.NumFramesAccumulatedHistory = nullptr;
		}

		if (bGuideByHistory)
		{
			GraphBuilder.QueueBufferExtraction(VisibleLightHash, &MegaLightsViewState.VisibleLightHashHistory);
			GraphBuilder.QueueBufferExtraction(VisibleLightMaskHash, &MegaLightsViewState.VisibleLightMaskHashHistory);
		}
		else
		{
			MegaLightsViewState.VisibleLightHashHistory = nullptr;
			MegaLightsViewState.VisibleLightMaskHashHistory = nullptr;
		}

		if (bVolumeGuideByHistory && VolumeVisibleLightHash != nullptr)
		{
			GraphBuilder.QueueBufferExtraction(VolumeVisibleLightHash, &MegaLightsViewState.VolumeVisibleLightHashHistory);
		}
		else
		{
			MegaLightsViewState.VolumeVisibleLightHashHistory = nullptr;
		}

		if (bTranslucencyVolumeGuideByHistory && TranslucencyVolumeVisibleLightHash[0] != nullptr && TranslucencyVolumeVisibleLightHash[1] != nullptr)
		{
			GraphBuilder.QueueBufferExtraction(TranslucencyVolumeVisibleLightHash[0], &MegaLightsViewState.TranslucencyVolume0VisibleLightHashHistory);
			GraphBuilder.QueueBufferExtraction(TranslucencyVolumeVisibleLightHash[1], &MegaLightsViewState.TranslucencyVolume1VisibleLightHashHistory);
		}
		else
		{
			MegaLightsViewState.TranslucencyVolume0VisibleLightHashHistory = nullptr;
			MegaLightsViewState.TranslucencyVolume1VisibleLightHashHistory = nullptr;
		}

		// Scene Depth/Normal history
		if (InputType == EMegaLightsInput::HairStrands)
		{
			if (SceneDepth)
			{
				GraphBuilder.QueueTextureExtraction(SceneDepth, &MegaLightsViewState.SceneDepthHistory);
			}
			else
			{
				MegaLightsViewState.SceneDepthHistory = nullptr;
			}

			if (SceneWorldNormal)
			{
				GraphBuilder.QueueTextureExtraction(SceneWorldNormal, &MegaLightsViewState.SceneNormalHistory);
			}
			else
			{
				MegaLightsViewState.SceneNormalHistory = nullptr;
			}
		}
	}
}
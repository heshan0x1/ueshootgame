// Copyright Epic Games, Inc. All Rights Reserved.

#include "LumenScreenProbeGather.h"
#include "RendererPrivate.h"
#include "ScenePrivate.h"
#include "SceneUtils.h"
#include "PipelineStateCache.h"
#include "ShaderParameterStruct.h"
#include "PixelShaderUtils.h"
#include "LumenShortRangeAO.h"

static TAutoConsoleVariable<int32> CVarLumenShortRangeAODownsampleFactor(
	TEXT("r.Lumen.ScreenProbeGather.ShortRangeAO.DownsampleFactor"),
	2,
	TEXT("Downsampling factor for ShortRangeAO."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarLumenShortRangeAOTemporal(
	TEXT("r.Lumen.ScreenProbeGather.ShortRangeAO.Temporal"),
	1,
	TEXT("Whether to run temporal accumulation on Short Range AO"),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarLumenShortRangeAOBentNormal(
	TEXT("r.Lumen.ScreenProbeGather.ShortRangeAO.BentNormal"),
	1,
	TEXT("Whether to use bent normal or just scalar AO. Scalar AO is slightly faster, but bent normal improves specular occlusion."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<float> CVarLumenShortRangeAOTemporalNeighborhoodClampScale(
	TEXT("r.Lumen.ScreenProbeGather.ShortRangeAO.Temporal.NeighborhoodClampScale"),
	1.0f,
	TEXT("Scales how permissive is neighborhood clamp. Higher values increase ghosting, but reduce noise and instability. Values <= 0 will disable neighborhood clamp."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

float GLumenShortRangeAOSlopeCompareToleranceScale = .5f;
FAutoConsoleVariableRef CVarLumenShortRangeAOSlopeCompareToleranceScale(
	TEXT("r.Lumen.ScreenProbeGather.ShortRangeAO.SlopeCompareToleranceScale"),
	GLumenShortRangeAOSlopeCompareToleranceScale,
	TEXT("Scales the slope threshold that screen space traces use to determine whether there was a hit."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<float> CVarLumenShortRangeAOFoliageOcclusionStrength(
	TEXT("r.Lumen.ScreenProbeGather.ShortRangeAO.FoliageOcclusionStrength"),
	0.7f,
	TEXT("Maximum strength of ScreenSpaceBentNormal occlusion on foliage and subsurface pixels.  Useful for reducing max occlusion to simulate subsurface scattering."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

float GLumenMaxShortRangeAOMultibounceAlbedo = .5f;
FAutoConsoleVariableRef CVarLumenMaxShortRangeAOMultibounceAlbedo(
	TEXT("r.Lumen.ScreenProbeGather.ShortRangeAO.MaxMultibounceAlbedo"),
	GLumenMaxShortRangeAOMultibounceAlbedo,
	TEXT("Maximum albedo used for the AO multi-bounce approximation.  Useful for forcing near-white albedo to have some occlusion."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GLumenShortRangeAOHairStrandsVoxelTrace = 1;
FAutoConsoleVariableRef GVarLumenShortRangeAOHairStrandsVoxelTrace(
	TEXT("r.Lumen.ScreenProbeGather.ShortRangeAO.HairVoxelTrace"),
	GLumenShortRangeAOHairStrandsVoxelTrace,
	TEXT("Whether to trace against hair voxel structure for hair casting shadow onto opaques."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GLumenShortRangeAOHairStrandsScreenTrace = 0;
FAutoConsoleVariableRef GVarShortRangeAOHairStrandsScreenTrace(
	TEXT("r.Lumen.ScreenProbeGather.ShortRangeAO.HairScreenTrace"),
	GLumenShortRangeAOHairStrandsScreenTrace,
	TEXT("Whether to trace against hair depth for hair casting shadow onto opaques."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GLumenShortRangeAOScreenSpaceHorizonSearch = 1;
FAutoConsoleVariableRef CVarLumenShortRangeAOScreenSpaceHorizonSearch(
	TEXT("r.Lumen.ScreenProbeGather.ShortRangeAO.HorizonSearch"),
	GLumenShortRangeAOScreenSpaceHorizonSearch,
	TEXT("0: Stochastic hemisphere integration with screen traces\n")
	TEXT("1: Search the depth buffer along view space slices for the occluded horizon."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GLumenShortRangeAOScreenSpaceHorizonSearchVisibilityBitmask = 0;
FAutoConsoleVariableRef CVarLumenShortRangeAOScreenSpaceHorizonSearchVisibilityBitmask(
	TEXT("r.Lumen.ScreenProbeGather.ShortRangeAO.HorizonSearch.VisibilityBitmask"),
	GLumenShortRangeAOScreenSpaceHorizonSearchVisibilityBitmask,
	TEXT("Whether to use a visibility bitmask for the horizon search instead of min/max horizon angles. This method has the potential to handle thin occluders better because it doesn't assume a continuous depth buffer."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GLumenShortRangeAOScreenSpaceHorizonSearchHZB = 1;
FAutoConsoleVariableRef CVarLumenShortRangeAOScreenSpaceHorizonSearchHZB(
	TEXT("r.Lumen.ScreenProbeGather.ShortRangeAO.HorizonSearch.HZB"),
	GLumenShortRangeAOScreenSpaceHorizonSearchHZB,
	TEXT("Whether to use the Hierarchical ZBuffer instead of SceneDepth for occlusion. HZB is slightly faster to sample, especially with a larger AO radius, but loses detail on grass and thin occluders since it is half res depth."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GLumenShortRangeAOSliceCount = 2;
FAutoConsoleVariableRef CVarLumenShortRangeAOSliceCount(
	TEXT("r.Lumen.ScreenProbeGather.ShortRangeAO.HorizonSearch.SliceCount"),
	GLumenShortRangeAOSliceCount,
	TEXT("Number of view space slices to search per pixel. This is a primary scalability control for Horizon ShortRangeAO."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GLumenShortRangeAOStepsPerSlice = 3;
FAutoConsoleVariableRef CVarLumenShortRangeAOStepsPerSlice(
	TEXT("r.Lumen.ScreenProbeGather.ShortRangeAO.HorizonSearch.StepsPerSlice"),
	GLumenShortRangeAOStepsPerSlice,
	TEXT("Number of horizon searching steps per view space slice. This is a primary scalability control for Horizon ShortRangeAO."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

float GLumenShortRangeAOForegroundSampleRejectDistanceFraction = .3f;
FAutoConsoleVariableRef CVarLumenShortRangeAOForegroundSampleRejectDistanceFraction(
	TEXT("r.Lumen.ScreenProbeGather.ShortRangeAO.HorizonSearch.ForegroundSampleRejectDistanceFraction"),
	GLumenShortRangeAOForegroundSampleRejectDistanceFraction,
	TEXT("Controls the Z distance away from the current pixel where neighboring pixels will be considered foreground and have their occlusion rejected, as a fraction of pixel depth."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

float GLumenShortRangeAOForegroundSampleRejectPower = 1.0f;
FAutoConsoleVariableRef CVarLumenShortRangeAOForegroundSampleRejectPower(
	TEXT("r.Lumen.ScreenProbeGather.ShortRangeAO.HorizonSearch.ForegroundSampleRejectPower"),
	GLumenShortRangeAOForegroundSampleRejectPower,
	TEXT("Controls how strongly foreground occluders contribute to final occlusion."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarLumenShortRangeAOApplyDuringIntegration(
	TEXT("r.Lumen.ScreenProbeGather.ShortRangeAO.ApplyDuringIntegration"),
	0,
	TEXT("Whether Screen Space Bent Normal should be applied during BRDF integration, which has higher quality but is before the temporal filter so causes streaking on moving objects."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GLumenShortRangeGlobalIllumination = 0;
FAutoConsoleVariableRef CVarLumenShortRangeGlobalIllumination(
	TEXT("r.Lumen.ScreenProbeGather.ShortRangeGI"),
	GLumenShortRangeGlobalIllumination,
	TEXT("Whether to calculate and apply Short Range Global Illumination, on top of Ambient Occlusion. Experimental feature, not ready for production. "),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

float GLumenShortRangeGIHistoryDepthTestRelativeThickness = 0.0005f;
FAutoConsoleVariableRef CVarLumenShortRangeGIHistoryDepthTestRelativeThickness(
	TEXT("r.Lumen.ScreenProbeGather.ShortRangeGI.HistoryDepthTestRelativeThickness"),
	GLumenShortRangeGIHistoryDepthTestRelativeThickness,
	TEXT("Distance between HZB trace hit and previous frame scene depth from which to allow hits, as a relative depth threshold."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

float GLumenShortRangeGIMaxScreenFraction = .1f;
FAutoConsoleVariableRef CVarLumenShortRangeGIMaxScreenFraction(
	TEXT("r.Lumen.ScreenProbeGather.ShortRangeGI.MaxScreenFraction"),
	GLumenShortRangeGIMaxScreenFraction,
	TEXT("Trace distance for GI as a fraction of the screen size."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

float GLumenShortRangeGIMaxRayIntensity = 5.0f;
FAutoConsoleVariableRef CVarLumenShortRangeGIMaxRayIntensity(
	TEXT("r.Lumen.ScreenProbeGather.ShortRangeGI.MaxRayIntensity"),
	GLumenShortRangeGIMaxRayIntensity,
	TEXT("Maximum intensity of a single sample. Used to clamp fireflies, also loses lighting energy."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

namespace LumenShortRangeAO
{
	bool ShouldApplyDuringIntegration()
	{
		return CVarLumenShortRangeAOApplyDuringIntegration.GetValueOnAnyThread() != 0;
	}

	bool UseBentNormal()
	{
		return CVarLumenShortRangeAOBentNormal.GetValueOnAnyThread() != 0;
	}

	EPixelFormat GetTextureFormat()
	{
		return LumenShortRangeAO::UseBentNormal() ? PF_R32_UINT : PF_R8;
	}

	uint32 GetRequestedDownsampleFactor()
	{
		return FMath::Clamp(CVarLumenShortRangeAODownsampleFactor.GetValueOnAnyThread(), 1, 2);
	}

	uint32 GetDownsampleFactor()
	{
		uint32 DownsampleFactor = GetRequestedDownsampleFactor();

		if (ShouldApplyDuringIntegration() && LumenScreenProbeGather::GetRequestedIntegrateDownsampleFactor() != DownsampleFactor)
		{
			return 1;
		}
		
		if (!ShouldApplyDuringIntegration() && !UseTemporal())
		{
			return 1;
		}

		return DownsampleFactor;
	}

	bool UseTemporal()
	{
		return CVarLumenShortRangeAOTemporal.GetValueOnAnyThread() != 0;
	}

	float GetTemporalNeighborhoodClampScale()
	{
		return CVarLumenShortRangeAOTemporalNeighborhoodClampScale.GetValueOnRenderThread();
	}

	float GetFoliageOcclusionStrength()
	{
		return FMath::Clamp(CVarLumenShortRangeAOFoliageOcclusionStrength.GetValueOnRenderThread(), 0.0f, 1.0f);
	}
}

class FScreenSpaceShortRangeAOCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FScreenSpaceShortRangeAOCS)
	SHADER_USE_PARAMETER_STRUCT(FScreenSpaceShortRangeAOCS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray, RWShortRangeAO)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray, RWShortRangeGI)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSceneTextureUniformParameters, SceneTexturesStruct)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenHZBScreenTraceParameters, HZBScreenTraceParameters)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSubstrateGlobalUniformParameters, Substrate)
		SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureParameters, SceneTextures)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		SHADER_PARAMETER_STRUCT_INCLUDE(ShaderPrint::FShaderParameters, ShaderPrintUniformBuffer)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, LightingChannelsTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, DownsampledSceneDepth)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<UNORM float3>, DownsampledWorldNormal)
		SHADER_PARAMETER_STRUCT_REF(FBlueNoise, BlueNoise)
		SHADER_PARAMETER(uint32, ScreenProbeGatherStateFrameIndex)
		SHADER_PARAMETER(FIntPoint, ShortRangeAOViewMin)
		SHADER_PARAMETER(FIntPoint, ShortRangeAOViewSize)
		SHADER_PARAMETER(float, SlopeCompareToleranceScale)
		SHADER_PARAMETER(FVector2f, MaxScreenFractionForAO)
		SHADER_PARAMETER(float, MaxScreenFractionForGI)
		SHADER_PARAMETER(float, MaxRayIntensity)
		SHADER_PARAMETER(float, ScreenTraceNoFallbackThicknessScale)
		SHADER_PARAMETER(float, HistoryDepthTestRelativeThickness)
		SHADER_PARAMETER(float, SliceCount)
		SHADER_PARAMETER(float, StepsPerSliceForAO)
		SHADER_PARAMETER(float, StepsPerSliceForGI)
		SHADER_PARAMETER(float, ForegroundSampleRejectDistanceFraction)
		SHADER_PARAMETER(float, ForegroundSampleRejectPower)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FHairStrandsViewUniformParameters, HairStrands)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FVirtualVoxelParameters, HairStrandsVoxel)
		RDG_BUFFER_ACCESS(TileIndirectBuffer, ERHIAccess::IndirectArgs)
	END_SHADER_PARAMETER_STRUCT()

	class FHorizonSearch : SHADER_PERMUTATION_BOOL("HORIZON_SEARCH");
	class FHorizonSearchVisibilityBitmask : SHADER_PERMUTATION_BOOL("HORIZON_SEARCH_VISIBILITY_BITMASK");
	class FHorizonSearchHZB : SHADER_PERMUTATION_BOOL("HORIZON_SEARCH_USE_HZB");
	class FShortRangeGI : SHADER_PERMUTATION_BOOL("SHORT_RANGE_GI");
	class FNumPixelRays : SHADER_PERMUTATION_SPARSE_INT("NUM_PIXEL_RAYS", 4, 8, 16);
	class FOverflow : SHADER_PERMUTATION_BOOL("PERMUTATION_OVERFLOW_TILE"); 
	class FHairStrandsScreen : SHADER_PERMUTATION_BOOL("USE_HAIRSTRANDS_SCREEN");
	class FHairStrandsVoxel : SHADER_PERMUTATION_BOOL("USE_HAIRSTRANDS_VOXEL");
	class FOutputBentNormal : SHADER_PERMUTATION_BOOL("OUTPUT_BENT_NORMAL");
	class FDownsampleFactor : SHADER_PERMUTATION_RANGE_INT("DOWNSAMPLE_FACTOR", 1, 2);
	class FUseDistanceFieldRepresentationBit : SHADER_PERMUTATION_BOOL("USE_DISTANCE_FIELD_REPRESENTATION_BIT");
	using FPermutationDomain = TShaderPermutationDomain<FHorizonSearch, FHorizonSearchVisibilityBitmask, FHorizonSearchHZB, FShortRangeGI, FNumPixelRays, FOverflow, FHairStrandsScreen, FHairStrandsVoxel, FOutputBentNormal, FDownsampleFactor, FUseDistanceFieldRepresentationBit>;

	static FPermutationDomain RemapPermutation(FPermutationDomain PermutationVector, const EShaderPlatform Platform)
	{
		if (!Substrate::IsSubstrateEnabled() || Substrate::IsSubstrateBlendableGBufferEnabled(Platform))
		{
			PermutationVector.Set<FOverflow>(false);
		}
		
		if (PermutationVector.Get<FHorizonSearch>())
		{
			PermutationVector.Set<FNumPixelRays>(4);
			PermutationVector.Set<FHairStrandsScreen>(false);
			PermutationVector.Set<FUseDistanceFieldRepresentationBit>(false);
		}
		else
		{
			PermutationVector.Set<FHorizonSearchHZB>(false);
			PermutationVector.Set<FHorizonSearchVisibilityBitmask>(false);
			PermutationVector.Set<FShortRangeGI>(false);
		}

		return PermutationVector;
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		FPermutationDomain PermutationVector(Parameters.PermutationId);
		if (RemapPermutation(PermutationVector, Parameters.Platform) != PermutationVector)
		{
			return false;
		}

		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}
	
	static EShaderPermutationPrecacheRequest ShouldPrecachePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		FPermutationDomain PermutationVector(Parameters.PermutationId);
				
		const bool bHorizonSearchHZB = GLumenShortRangeAOScreenSpaceHorizonSearchHZB != 0;
		const bool bVisibilityBitmask = GLumenShortRangeAOScreenSpaceHorizonSearchVisibilityBitmask != 0;
		const bool bShortRangeGI = GLumenShortRangeGlobalIllumination != 0;
		const bool bHorizonSearch = GLumenShortRangeAOScreenSpaceHorizonSearch == 1 || bShortRangeGI;
		const bool bUseBentNormal = LumenShortRangeAO::UseBentNormal() ? 1 : 0;
		const int32 DownsampleFactor = LumenShortRangeAO::GetDownsampleFactor();

		const bool bCanUseTraceHairVoxel = GLumenShortRangeAOHairStrandsVoxelTrace > 0;
		const bool bCanUseTraceHairScreen = GLumenShortRangeAOHairStrandsScreenTrace > 0;
		
		if (PermutationVector.Get<FHorizonSearch>() != bHorizonSearch)
		{
			return EShaderPermutationPrecacheRequest::NotPrecached;
		}

		if (PermutationVector.Get<FShortRangeGI>() != bShortRangeGI)
		{
			return EShaderPermutationPrecacheRequest::NotPrecached;
		}

		if (PermutationVector.Get<FHorizonSearchHZB>() != bHorizonSearchHZB)
		{
			return EShaderPermutationPrecacheRequest::NotPrecached;
		}

		if (PermutationVector.Get<FHorizonSearchVisibilityBitmask>() != bVisibilityBitmask)
		{
			return EShaderPermutationPrecacheRequest::NotPrecached;
		}

		if (PermutationVector.Get<FOutputBentNormal>() != bUseBentNormal)
		{
			return EShaderPermutationPrecacheRequest::NotPrecached;
		}

		if (PermutationVector.Get<FDownsampleFactor>() != DownsampleFactor)
		{
			return EShaderPermutationPrecacheRequest::NotPrecached;
		}
				
		if (PermutationVector.Get<FHairStrandsVoxel>() && !bCanUseTraceHairVoxel)
		{
			return EShaderPermutationPrecacheRequest::NotUsed;
		}

		if (PermutationVector.Get<FHairStrandsScreen>() && !bCanUseTraceHairScreen)
		{
			return EShaderPermutationPrecacheRequest::NotPrecached;
		}

		return EShaderPermutationPrecacheRequest::Precached;
	}

	static int32 GetGroupSize() 
	{
		// Sanity check
		static_assert(8 == SUBSTRATE_TILE_SIZE);
		return 8;
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GetGroupSize());
		OutEnvironment.CompilerFlags.Add(CFLAG_Wave32);
	}
};

IMPLEMENT_GLOBAL_SHADER(FScreenSpaceShortRangeAOCS, "/Engine/Private/Lumen/LumenScreenSpaceBentNormal.usf", "ScreenSpaceShortRangeAOCS", SF_Compute);

FLumenScreenSpaceBentNormalParameters ComputeScreenSpaceShortRangeAO(
	FRDGBuilder& GraphBuilder, 
	const FScene* Scene,
	const FViewInfo& View, 
	const FLumenSceneFrameTemporaries& FrameTemporaries,
	const FSceneTextures& SceneTextures,
	FRDGTextureRef LightingChannelsTexture,
	const FBlueNoise& BlueNoise,
	FVector2f MaxScreenTraceFraction,
	float ScreenTraceNoFallbackThicknessScale,
	ERDGPassFlags ComputePassFlags)
{
	const FSceneTextureParameters& SceneTextureParameters = GetSceneTextureParameters(GraphBuilder, SceneTextures);

	// When Substrate is enabled, increase the resolution for multi-layer tile overflowing (tile containing multi-BSDF data)
	const int32 DownsampleFactor = LumenShortRangeAO::GetDownsampleFactor();
	FIntPoint ShortRangeAOBufferSize = Substrate::GetSubstrateTextureResolution(View, FIntPoint::DivideAndRoundUp(View.GetSceneTexturesConfig().Extent, DownsampleFactor));
	FIntPoint ShortRangeAOViewMin = FIntPoint::DivideAndRoundUp(View.ViewRect.Min, DownsampleFactor);
	FIntPoint ShortRangeAOViewSize = FIntPoint::DivideAndRoundUp(View.ViewRect.Size(), DownsampleFactor);
	const uint32 ClosureCount = Substrate::GetSubstrateMaxClosureCount(View);

	FLumenScreenSpaceBentNormalParameters OutParameters;
	OutParameters.ShortRangeAOViewMin = ShortRangeAOViewMin;
	OutParameters.ShortRangeAOViewSize = ShortRangeAOViewSize;

	FRDGTextureRef ShortRangeAO = GraphBuilder.CreateTexture(
		FRDGTextureDesc::Create2DArray(ShortRangeAOBufferSize, LumenShortRangeAO::GetTextureFormat(), FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV, ClosureCount),
		TEXT("Lumen.ScreenProbeGather.ShortRangeAO"));

	const bool bShortRangeGI = GLumenShortRangeGlobalIllumination != 0;
	FRDGTextureRef ShortRangeGI = GraphBuilder.CreateTexture(
		FRDGTextureDesc::Create2DArray(ShortRangeAOBufferSize, PF_FloatRGB, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV, ClosureCount),
		TEXT("Lumen.ScreenProbeGather.ShortRangeGI"));

	int32 NumPixelRays = 4;

	if (View.FinalPostProcessSettings.LumenFinalGatherQuality >= 6.0f)
	{
		NumPixelRays = 16;
	}
	else if (View.FinalPostProcessSettings.LumenFinalGatherQuality >= 2.0f)
	{
		NumPixelRays = 8;
	}

	if (Lumen::UseHardwareRayTracedShortRangeAO(*View.Family))
	{
		RenderHardwareRayTracingShortRangeAO(
			GraphBuilder,
			Scene,
			SceneTextures,
			SceneTextureParameters,
			FrameTemporaries,
			OutParameters,
			BlueNoise,
			MaxScreenTraceFraction.X,
			View,
			ShortRangeAO,
			NumPixelRays);
	}
	else
	{
		const bool bNeedTraceHairVoxel = HairStrands::HasViewHairStrandsVoxelData(View) && GLumenShortRangeAOHairStrandsVoxelTrace > 0;
		const bool bNeedTraceHairScreen = HairStrands::HasViewHairStrandsData(View) && GLumenShortRangeAOHairStrandsScreenTrace > 0;
		const bool bUseHardwareRayTracing = Lumen::UseHardwareRayTracedScreenProbeGather(*View.Family);
		
		auto ScreenSpaceShortRangeAO = [&](bool bOverflow)
		{
			FScreenSpaceShortRangeAOCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FScreenSpaceShortRangeAOCS::FParameters>();
			PassParameters->RWShortRangeAO = GraphBuilder.CreateUAV(ShortRangeAO);
			PassParameters->RWShortRangeGI = GraphBuilder.CreateUAV(ShortRangeGI);
			PassParameters->DownsampledSceneDepth = FrameTemporaries.DownsampledSceneDepth2x2.GetRenderTarget();
			PassParameters->DownsampledWorldNormal = FrameTemporaries.DownsampledWorldNormal2x2.GetRenderTarget();
			PassParameters->SceneTexturesStruct = SceneTextures.UniformBuffer;
			PassParameters->HZBScreenTraceParameters = SetupHZBScreenTraceParameters(GraphBuilder, View, SceneTextures);
			PassParameters->Substrate = Substrate::BindSubstrateGlobalUniformParameters(View);
			PassParameters->SceneTextures = SceneTextureParameters;

			if (PassParameters->HZBScreenTraceParameters.PrevSceneColorTexture->GetParent() == SceneTextures.Color.Resolve || !PassParameters->SceneTextures.GBufferVelocityTexture)
			{
				PassParameters->SceneTextures.GBufferVelocityTexture = GSystemTextures.GetBlackDummy(GraphBuilder);
			}

			PassParameters->HZBScreenTraceParameters.HZBParameters = GetHZBParameters(GraphBuilder, View, EHZBType::FurthestHZB);
			PassParameters->MaxScreenFractionForAO = MaxScreenTraceFraction;
			PassParameters->MaxScreenFractionForGI = FMath::Clamp<float>(GLumenShortRangeGIMaxScreenFraction, PassParameters->MaxScreenFractionForAO.X, 1.0f);
			PassParameters->MaxRayIntensity = GLumenShortRangeGIMaxRayIntensity;
			PassParameters->ScreenTraceNoFallbackThicknessScale = ScreenTraceNoFallbackThicknessScale;
			PassParameters->HistoryDepthTestRelativeThickness = GLumenShortRangeGIHistoryDepthTestRelativeThickness;
			PassParameters->ViewUniformBuffer = View.ViewUniformBuffer;
			ShaderPrint::SetParameters(GraphBuilder, View.ShaderPrintData, PassParameters->ShaderPrintUniformBuffer);
			PassParameters->LightingChannelsTexture = LightingChannelsTexture;
			PassParameters->BlueNoise = CreateUniformBufferImmediate(BlueNoise, EUniformBufferUsage::UniformBuffer_SingleDraw);
			PassParameters->ScreenProbeGatherStateFrameIndex = LumenScreenProbeGather::GetStateFrameIndex(View.ViewState);
			PassParameters->ShortRangeAOViewMin = ShortRangeAOViewMin;
			PassParameters->ShortRangeAOViewSize = ShortRangeAOViewSize;
			PassParameters->SlopeCompareToleranceScale = GLumenShortRangeAOSlopeCompareToleranceScale;

			if (bNeedTraceHairScreen)
			{
				PassParameters->HairStrands = HairStrands::BindHairStrandsViewUniformParameters(View);
			}

			if (bNeedTraceHairVoxel)
			{
				PassParameters->HairStrandsVoxel = HairStrands::BindHairStrandsVoxelUniformParameters(View);
			}

			PassParameters->SliceCount = GLumenShortRangeAOSliceCount;
			PassParameters->StepsPerSliceForAO = GLumenShortRangeAOStepsPerSlice;
			PassParameters->StepsPerSliceForGI = FMath::CeilToFloat(PassParameters->StepsPerSliceForAO * sqrt(PassParameters->MaxScreenFractionForGI / PassParameters->MaxScreenFractionForAO.X));
			PassParameters->ForegroundSampleRejectDistanceFraction = GLumenShortRangeAOForegroundSampleRejectDistanceFraction;
			PassParameters->ForegroundSampleRejectPower = FMath::Clamp<float>(GLumenShortRangeAOForegroundSampleRejectPower, .1f, 10.0f);
			const bool bHorizonSearch = GLumenShortRangeAOScreenSpaceHorizonSearch == 1 || bShortRangeGI;
			const bool bVisibilityBitmask = GLumenShortRangeAOScreenSpaceHorizonSearchVisibilityBitmask != 0;

			FScreenSpaceShortRangeAOCS::FPermutationDomain PermutationVector;
			PermutationVector.Set<FScreenSpaceShortRangeAOCS::FHorizonSearch>(bHorizonSearch);
			PermutationVector.Set<FScreenSpaceShortRangeAOCS::FHorizonSearchHZB>(GLumenShortRangeAOScreenSpaceHorizonSearchHZB != 0);
			PermutationVector.Set<FScreenSpaceShortRangeAOCS::FHorizonSearchVisibilityBitmask>(bVisibilityBitmask);
			PermutationVector.Set<FScreenSpaceShortRangeAOCS::FShortRangeGI>(bShortRangeGI);
			PermutationVector.Set<FScreenSpaceShortRangeAOCS::FNumPixelRays >(NumPixelRays);
			PermutationVector.Set<FScreenSpaceShortRangeAOCS::FOverflow>(bOverflow);
			PermutationVector.Set<FScreenSpaceShortRangeAOCS::FHairStrandsScreen>(bNeedTraceHairScreen);
			PermutationVector.Set<FScreenSpaceShortRangeAOCS::FHairStrandsVoxel>(bNeedTraceHairVoxel);
			PermutationVector.Set<FScreenSpaceShortRangeAOCS::FOutputBentNormal>(LumenShortRangeAO::UseBentNormal() ? 1 : 0);
			PermutationVector.Set<FScreenSpaceShortRangeAOCS::FDownsampleFactor>(DownsampleFactor);
			PermutationVector.Set<FScreenSpaceShortRangeAOCS::FUseDistanceFieldRepresentationBit>(Lumen::IsUsingDistanceFieldRepresentationBit(View));
			PermutationVector = FScreenSpaceShortRangeAOCS::RemapPermutation(PermutationVector, View.GetShaderPlatform());
			auto ComputeShader = View.ShaderMap->GetShader<FScreenSpaceShortRangeAOCS>(PermutationVector);

			if (bOverflow)
			{
				PassParameters->TileIndirectBuffer = View.SubstrateViewData.ClosureTileDispatchIndirectBuffer;
				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME("ShortRangeAO_ScreenSpace Overflow"),
					ComputePassFlags,
					ComputeShader,
					PassParameters,
					View.SubstrateViewData.ClosureTileDispatchIndirectBuffer,
					Substrate::GetClosureTileIndirectArgsOffset(DownsampleFactor));
			}
			else
			{
				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME("ShortRange%s_ScreenSpace%s(Rays=%u, Steps=%u, DownsampleFactor:%d, BentNormal:%d)", bShortRangeGI ? TEXT("GI") : TEXT("AO"), (bHorizonSearch ? (bVisibilityBitmask ? TEXT("_VisibilityBitmask") : TEXT("_HorizonSearch")) : TEXT("_StochasticHemisphere")), bHorizonSearch ? (uint32)PassParameters->SliceCount : NumPixelRays, bHorizonSearch ? (bShortRangeGI ? (uint32)PassParameters->StepsPerSliceForGI : (uint32)PassParameters->StepsPerSliceForAO) : 4, DownsampleFactor, LumenShortRangeAO::UseBentNormal()),
					ComputePassFlags,
					ComputeShader,
					PassParameters,
					FComputeShaderUtils::GetGroupCount(ShortRangeAOViewSize, FScreenSpaceShortRangeAOCS::GetGroupSize()));
			}
		};

		ScreenSpaceShortRangeAO(false);
		if (Lumen::SupportsMultipleClosureEvaluation(View))
		{
			ScreenSpaceShortRangeAO(true);
		}
	}

	OutParameters.ShortRangeAOTexture = ShortRangeAO;
	OutParameters.ShortRangeGITexture = bShortRangeGI ? ShortRangeGI : nullptr;
	OutParameters.ShortRangeAOMode = LumenShortRangeAO::UseBentNormal() ? 2 : 1;
	return OutParameters;
}
// Copyright Epic Games, Inc. All Rights Reserved.

#include "MegaLights.h"
#include "MegaLightsInternal.h"
#include "RendererPrivate.h"
#include "PixelShaderUtils.h"
#include "BasePassRendering.h"
#include "VolumetricFogShared.h"
#include "Shadows/ShadowSceneRenderer.h"
#include "HairStrands/HairStrandsData.h"
#include "StochasticLighting/StochasticLighting.h"

static TAutoConsoleVariable<int32> CVarMegaLightsProjectSetting(
	TEXT("r.MegaLights.EnableForProject"),
	0,
	TEXT("Whether to use MegaLights by default, but this can still be overridden by Post Process Volumes, or disabled per-light. MegaLights uses stochastic sampling to render many shadow casting lights efficiently, with a consistent low GPU cost. MegaLights requires Hardware Ray Tracing, and does not support Directional Lights. Experimental feature."),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarMegaLightsAllowed(
	TEXT("r.MegaLights.Allowed"),
	1,
	TEXT("Whether the MegaLights feature is allowed by scalability and device profiles."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarMegaLightsLightingDataFormat(
	TEXT("r.MegaLights.LightingDataFormat"),
	0,
	TEXT("Data format for surfaces storing lighting information (e.g. radiance, irradiance).\n")
	TEXT("0 - Float_R11G11B10 (fast default)\n")
	TEXT("1 - Float16_RGBA (slow but higher precision, mostly for testing)\n")
	TEXT("2 - Float32_RGBA (reference for testing)"),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarMegaLightsDownsampleMode(
	TEXT("r.MegaLights.DownsampleMode"),
	2,
	TEXT("Downsample mode from the main viewport to sample and trace rays. Increases performance, but reduces quality.\n")
	TEXT("0 - Disabled (1x1)\n")
	TEXT("1 - Checkerboard (2x1)\n")
	TEXT("2 - Half-resolution (2x2)"),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarMegaLightsNumSamplesPerPixel(
	TEXT("r.MegaLights.NumSamplesPerPixel"),
	4,
	TEXT("Number of samples per pixel. Supported values: 2, 4 and 16."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<float> CVarMegaLightsMinSampleWeight(
	TEXT("r.MegaLights.MinSampleWeight"),
	0.001f,
	TEXT("Determines minimal sample influence on final pixels. Used to skip samples which would have minimal impact to the final image even if light is fully visible."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<float> CVarMegaLightsMaxShadingWeight(
	TEXT("r.MegaLights.MaxShadingWeight"),
	20.0f,
	TEXT("Clamps low-probability samples in order to reduce fireflies."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarMegaLightsGuideByHistory(
	TEXT("r.MegaLights.GuideByHistory"),
	2,
	TEXT("Whether to reduce sampling chance for lights which were hidden last frame. Reduces noise in areas where bright lights are shadowed.\n")
	TEXT("0 - disabled\n")
	TEXT("1 - more rays towards visible lights\n")
	TEXT("2 - more rays towards visible parts of lights"),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarMegaLightsWaveOps(
	TEXT("r.MegaLights.WaveOps"),
	1,
	TEXT("Whether to use wave ops. Useful for debugging."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarMegaLightsDebug(
	TEXT("r.MegaLights.Debug"),
	0,
	TEXT("Whether to enabled debug mode, which prints various extra debug information from shaders.")
	TEXT("0 - Disable\n")
	TEXT("1 - Visualize tracing\n")
	TEXT("2 - Visualize sampling"),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarMegaLightsDebugCursorX(
	TEXT("r.MegaLights.Debug.CursorX"),
	-1,
	TEXT("Override default debug visualization cursor position."),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarMegaLightsDebugCursorY(
	TEXT("r.MegaLights.Debug.CursorY"),
	-1,
	TEXT("Override default debug visualization cursor position."),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarMegaLightsDebugLightId(
	TEXT("r.MegaLights.Debug.LightId"),
	-1,
	TEXT("Which light to show debug info for. When set to -1, uses the currently selected light in editor."),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarMegaLightsDebugVisualizeLight(
	TEXT("r.MegaLights.Debug.VisualizeLight"),
	0,
	TEXT("Whether to visualize selected light. Useful to find in in the level."),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarMegaLightsDebugVisualizeLightLoopIterations(
	TEXT("r.MegaLights.Debug.VisualizeLightLoopIterations"),
	0,
	TEXT("Whether to visualize light loop iterations.\n")
	TEXT("0 - Disable\n")
	TEXT("1 - Visualize ShadeLightSamplesCS light loop iterations\n")
	TEXT("2 - Visualize GenerateLightSamplesCS light loop iterations"),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarMegaLightsDebugTileClassification(
	TEXT("r.MegaLights.Debug.TileClassification"),
	0,
	TEXT("Whether to visualize tile classification.")
	TEXT("0 - Disable\n")
	TEXT("1 - Visualize tiles\n")
	TEXT("2 - Visualize downsampled tiles"),
	ECVF_RenderThreadSafe
);

int32 GMegaLightsReset = 0;
FAutoConsoleVariableRef CVarMegaLightsReset(
	TEXT("r.MegaLights.Reset"),
	GMegaLightsReset,
	TEXT("Reset history for debugging."),
	ECVF_RenderThreadSafe
);

int32 GMegaLightsResetEveryNthFrame = 0;
	FAutoConsoleVariableRef CVarMegaLightsResetEveryNthFrame(
	TEXT("r.MegaLights.ResetEveryNthFrame"),
		GMegaLightsResetEveryNthFrame,
	TEXT("Reset history every Nth frame for debugging."),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarMegaLightsFixedStateFrameIndex(
	TEXT("r.MegaLights.FixedStateFrameIndex"),
	-1,
	TEXT("Whether to override View.StateFrameIndex for debugging."),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarMegaLightsTexturedRectLights(
	TEXT("r.MegaLights.TexturedRectLights"),
	1,
	TEXT("Whether to support textured rect lights."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarMegaLightsLightFunctions(
	TEXT("r.MegaLights.LightFunctions"),
	1,
	TEXT("Whether to support light functions."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<bool> CVarMegaLightLightingChannels(
	TEXT("r.MegaLights.LightingChannels"),
	1,
	TEXT("Whether to enable lighting channels to block shadowing"),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarMegaLightsIESProfiles(
	TEXT("r.MegaLights.IESProfiles"),
	1,
	TEXT("Whether to support IES profiles on lights."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarMegaLightsDirectionalLights(
	TEXT("r.MegaLights.DirectionalLights"),
	0,
	TEXT("Whether to support directional lights."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarMegaLightsVolume(
	TEXT("r.MegaLights.Volume"),
	1,
	TEXT("Whether to enable a translucency volume used for Volumetric Fog and Volume Lit Translucency."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarMegaLightsVolumeUnified(
	TEXT("r.MegaLights.Volume.Unified"),
	1,
	TEXT("Whether to reuse sampling / tracing for volumetric fog and translucency volume."),
	ECVF_Scalability | ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarMegaLightsVolumeDepthDistributionScale(
	TEXT("r.MegaLights.Volume.DepthDistributionScale"),
	32.0f,
	TEXT("Scales the slice depth distribution."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarMegaLightsVolumeGridPixelSize(
	TEXT("r.MegaLights.Volume.GridPixelSize"),
	8,
	TEXT("XY Size of a cell in the voxel grid, in pixels."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarMegaLightsVolumeGridSizeZ(
	TEXT("r.MegaLights.Volume.GridSizeZ"),
	128,
	TEXT("How many Volumetric Fog cells to use in z."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarMegaLightsVolumeDownsampleMode(
	TEXT("r.MegaLights.Volume.DownsampleMode"),
	2,
	TEXT("Downsample mode applied for volume (Volumetric Fog and Lit Translucency) to sample and trace rays. Increases performance, but reduces quality.\n")
	TEXT("0 - Disabled (1x1x1)\n")
	TEXT("1 - Reserved for a future mode\n")
	TEXT("2 - Half-resolution (2x2x2)"),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarMegaLightsVolumeHZBOcclusionTest(
	TEXT("r.MegaLights.Volume.HZBOcclusionTest"),
	1,
	TEXT("Whether to skip computation for cells occluded by HZB."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarMegaLightsVolumeNumSamplesPerVoxel(
	TEXT("r.MegaLights.Volume.NumSamplesPerVoxel"),
	2,
	TEXT("Number of samples (shadow rays) per half-res voxel. Supported values: 2 and 4."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<float> CVarMegaLightsVolumeMinSampleWeight(
	TEXT("r.MegaLights.Volume.MinSampleWeight"),
	0.1f,
	TEXT("Determines minimal sample influence on lighting cached in a volume. Used to skip samples which would have minimal impact to the final image even if light is fully visible."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<float> CVarMegaLightsVolumeMaxShadingWeight(
	TEXT("r.MegaLights.Volume.MaxShadingWeight"),
	20.0f,
	TEXT("Clamps low-probability samples in order to reduce fireflies."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarMegaLightsVolumeLightFunctions(
	TEXT("r.MegaLights.Volume.LightFunctions"),
	1,
	TEXT("Whether to support light functions inside the mega light translucency volume."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarMegaLightsVolumeGuideByHistory(
	TEXT("r.MegaLights.Volume.GuideByHistory"),
	1,
	TEXT("Whether to reduce sampling chance for lights which were hidden last frame. Reduces noise in areas where bright lights are shadowed.\n")
	TEXT("0 - disabled\n")
	TEXT("1 - more rays towards visible lights"),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarMegaLightsVolumeDebug(
	TEXT("r.MegaLights.Volume.Debug"),
	0,
	TEXT("Whether to enabled debug mode, which prints various extra debug information from volume shaders.")
	TEXT("0 - Disable\n")
	TEXT("1 - Visualize tracing\n")
	TEXT("2 - Visualize sampling"),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarMegaLightsVolumeDebugSliceIndex(
	TEXT("r.MegaLights.Volume.DebugSliceIndex"),
	16,
	TEXT("Which volume slice to visualize."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarMegaLightsTranslucencyVolume(
	TEXT("r.MegaLights.TranslucencyVolume"),
	1,
	TEXT("Whether to enable Lit Translucency Volume."),
	ECVF_Scalability | ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarMegaLightsTranslucencyVolumeDownsampleFactor(
	TEXT("r.MegaLights.TranslucencyVolume.DownsampleFactor"),
	2,
	TEXT("Downsample factor applied to Translucency Lighting Volume resolution. Affects the resolution at which rays are traced."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarMegaLightsTranslucencyVolumeNumSamplesPerVoxel(
	TEXT("r.MegaLights.TranslucencyVolume.NumSamplesPerVoxel"),
	2,
	TEXT("Number of samples (shadow rays) per half-res voxel. Supported values: 2 and 4."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<float> CVarMegaLightsTranslucencyVolumeMinSampleWeight(
	TEXT("r.MegaLights.TranslucencyVolume.MinSampleWeight"),
	0.1f,
	TEXT("Determines minimal sample influence on lighting cached in a volume. Used to skip samples which would have minimal impact to the final image even if light is fully visible."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<float> CVarMegaLightsTranslucencyVolumeMaxShadingWeight(
	TEXT("r.MegaLights.TranslucencyVolume.MaxShadingWeight"),
	20.0f,
	TEXT("Clamps low-probability samples in order to reduce fireflies."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarMegaLightsTranslucencyVolumeLightFunctions(
	TEXT("r.MegaLights.TranslucencyVolume.LightFunctions"),
	1,
	TEXT("Whether to support light functions inside the mega light translucency volume."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarMegaLightsTranslucencyVolumeSpatial(
	TEXT("r.MegaLights.TranslucencyVolume.Spatial"),
	1,
	TEXT("Whether to run a spatial filter when updating the translucency volume."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarMegaLightsTranslucencyVolumeTemporal(
	TEXT("r.MegaLights.TranslucencyVolume.Temporal"),
	1,
	TEXT("Whether to use temporal accumulation when updating the translucency volume."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarMegaLightsTranslucencyVolumeGuideByHistory(
	TEXT("r.MegaLights.TranslucencyVolume.GuideByHistory"),
	1,
	TEXT("Whether to reduce sampling chance for lights which were hidden last frame. Reduces noise in areas where bright lights are shadowed.\n")
	TEXT("0 - disabled\n")
	TEXT("1 - more rays towards visible lights"),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarMegaLightsTranslucencyVolumeDebug(
	TEXT("r.MegaLights.TranslucencyVolume.Debug"),
	0,
	TEXT("Whether to enabled debug mode, which prints various extra debug information from Translucency Volume shaders.")
	TEXT("0 - Disable\n")
	TEXT("1 - Visualize tracing\n")
	TEXT("2 - Visualize sampling"),
	ECVF_RenderThreadSafe);

// Rendering project setting
int32 GMegaLightsDefaultShadowMethod = 0;
FAutoConsoleVariableRef CMegaLightsDefaultShadowMethod(
	TEXT("r.MegaLights.DefaultShadowMethod"),
	GMegaLightsDefaultShadowMethod,
	TEXT("The default shadowing method for MegaLights, unless over-ridden on the light component.\n")
	TEXT("0 - Ray Tracing. Preferred method, which guarantees fixed MegaLights cost and correct area shadows, but is dependent on the BVH representation quality.\n")
	TEXT("1 - Virtual Shadow Maps. Has a significant per light cost, but can cast shadows directly from the Nanite geometry using rasterization."),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarMegaLightsEnableHairStrands(
	TEXT("r.MegaLights.HairStrands"),
	1,
	TEXT("Wheter to enable hair strands support for MegaLights."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarMegaLightsHairStrandsDownsampleMode(
	TEXT("r.MegaLights.HairStrands.DownsampleMode"),
	0,
	TEXT("Downsample mode from the main viewport to sample and trace rays for hair strands. Increases performance, but reduces quality.\n")
	TEXT("0 - Disabled (1x1)\n")
	TEXT("1 - Checkerboard (2x1)\n")
	TEXT("2 - Half-resolution (2x2)"),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarMegaLightsNumSamplesPerPixelHairStrands(
	TEXT("r.MegaLights.HairStrands.NumSamplesPerPixel"),
	4,
	TEXT("Number of samples per pixel with hair strands. Supported values: 2, 4 and 16."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarMegaLightsDebugHairStrands(
	TEXT("r.MegaLights.HairStrands.Debug"),
	0,
	TEXT("Whether to enabled debug mode for hairstrands, which prints various extra debug information from shaders.")
	TEXT("0 - Disable\n")
	TEXT("1 - Visualize tracing\n")
	TEXT("2 - Visualize sampling"),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarMegaLightsHairStrandsSubPixelShading(
	TEXT("r.MegaLights.HairStrands.SubPixelShading"),
	0,
	TEXT("Shader all sub-pixel data for better quality (add extra cost)."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarMegaLightsReferenceOffsetToStateFrameIndex(
	TEXT("r.MegaLights.Reference.OffsetToStateFrameIndex"),
	0,
	TEXT("Offset to add to View.StateFrameIndex."),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarMegaLightsReferenceShadingPassCount(
	TEXT("r.MegaLights.Reference.NumShadingPass"),
	1,
	TEXT("Number of pass for shading (to generate references at the cost of performance when pass count is > 1)"),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarMegaLightsReferenceDebuggedPassIndex(
	TEXT("r.MegaLights.Reference.DebuggedPassIndex"),
	-1,
	TEXT("When r.MegaLights.Debug is activated, the pass index to print debug info from.\n.")
	TEXT("Use negative value to index in reverse order.\n.")
	TEXT("Default is -1 meaning the last pass.\n."),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarMegaLightsVSMMarkPages(
	TEXT("r.MegaLights.VSM.MarkPages"),
	1,
	TEXT("When enabled, MegaLights will mark Virtual Shadow Map pages for required samples directly.\n")
	TEXT("Otherwise any light using MegaLights VSM will mark all pages that conservatively might be required."),
	ECVF_RenderThreadSafe
);

extern int32 GUseTranslucencyLightingVolumes;

namespace MegaLights
{
	constexpr int32 TileSize = TILE_SIZE;
	constexpr int32 VisibleLightHashSize = VISIBLE_LIGHT_HASH_SIZE;
	constexpr int32 VisibleLightHashTileSize = VISIBLE_LIGHT_HASH_TILE_SIZE;

	bool ShouldCompileShaders(EShaderPlatform ShaderPlatform)
	{
		if (IsMobilePlatform(ShaderPlatform))
		{
			return false;
		}

		// SM6 because it uses typed loads to accumulate lights
		return IsFeatureLevelSupported(ShaderPlatform, ERHIFeatureLevel::SM6)
			&& RHISupportsWaveOperations(ShaderPlatform) 
			&& RHISupportsRayTracing(ShaderPlatform);
	}

	bool IsRequested(const FSceneViewFamily& ViewFamily)
	{
		return ViewFamily.Views[0]->FinalPostProcessSettings.bMegaLights
			&& CVarMegaLightsAllowed.GetValueOnRenderThread() != 0
			&& ViewFamily.EngineShowFlags.Lighting
			&& ViewFamily.EngineShowFlags.MegaLights
			&& ShouldCompileShaders(ViewFamily.GetShaderPlatform());
	}

	bool HasRequiredTracingData(const FSceneViewFamily& ViewFamily)
	{
		return IsHardwareRayTracingSupported(ViewFamily) || IsSoftwareRayTracingSupported(ViewFamily);
	}

	bool IsEnabled(const FSceneViewFamily& ViewFamily)
	{
		return IsRequested(ViewFamily) && HasRequiredTracingData(ViewFamily);
	}

	EPixelFormat GetLightingDataFormat()
	{
		if (CVarMegaLightsLightingDataFormat.GetValueOnRenderThread() == 2)
		{
			return PF_A32B32G32R32F;
		}
		else if (CVarMegaLightsLightingDataFormat.GetValueOnRenderThread() == 1)
		{
			return PF_FloatRGBA;
		}
		else
		{
			return PF_FloatR11G11B10;
		}
	}

	uint32 GetSampleMargin()
	{
		// #ml_todo: should be calculated based on DownsampleFactor / Volume.DownsampleFactor
		return 3;
	}

	bool UseVolume()
	{
		return CVarMegaLightsVolume.GetValueOnRenderThread() != 0;
	}

	bool UseTranslucencyVolume()
	{
		return CVarMegaLightsTranslucencyVolume.GetValueOnRenderThread() != 0;
	}

	bool IsTranslucencyVolumeSpatialFilterEnabled()
	{
		return CVarMegaLightsTranslucencyVolumeSpatial.GetValueOnRenderThread() != 0;
	}

	bool IsTranslucencyVolumeTemporalFilterEnabled()
	{
		return CVarMegaLightsTranslucencyVolumeTemporal.GetValueOnRenderThread() != 0;
	}

	bool IsMarkingVSMPages()
	{
		return CVarMegaLightsVSMMarkPages.GetValueOnRenderThread() != 0;
	}

	bool IsUsingLightFunctions(const FSceneViewFamily& ViewFamily)
	{
		return IsEnabled(ViewFamily) && CVarMegaLightsLightFunctions.GetValueOnRenderThread() != 0;
	}

	bool IsUsingLightingChannels()
	{
		return CVarMegaLightLightingChannels.GetValueOnRenderThread() != 0;
	}

	EMegaLightsMode GetMegaLightsMode(const FSceneViewFamily& ViewFamily, uint8 LightType, bool bLightAllowsMegaLights, TEnumAsByte<EMegaLightsShadowMethod::Type> ShadowMethod)
	{
		if ((LightType != LightType_Directional || CVarMegaLightsDirectionalLights.GetValueOnRenderThread())
			&& IsEnabled(ViewFamily) 
			&& bLightAllowsMegaLights)
		{
			// Resolve  default
			if (ShadowMethod == EMegaLightsShadowMethod::Default)
			{
				if (GMegaLightsDefaultShadowMethod == 1)
				{
					ShadowMethod = EMegaLightsShadowMethod::VirtualShadowMap;
				}
				else
				{
					ShadowMethod = EMegaLightsShadowMethod::RayTracing;
				}
			}

			const bool bUseVSM = ShadowMethod == EMegaLightsShadowMethod::VirtualShadowMap;

			if (bUseVSM)
			{
				return EMegaLightsMode::EnabledVSM;
			}
			// Just check first view, assuming the ray tracing flag is the same for all views.  See comment in the ShouldRenderRayTracingEffect function that accepts a ViewFamily.
			else if (ViewFamily.Views[0]->IsRayTracingAllowedForView())
			{
				return EMegaLightsMode::EnabledRT;
			}
		}

		return EMegaLightsMode::Disabled;
	}

	bool ShouldCompileShadersForReferenceMode(EShaderPlatform InPlatform)
	{
		// Only compile reference mode on PC platform
		return IsPCPlatform(InPlatform);
	}

	uint32 GetReferenceShadingPassCount(EShaderPlatform InPlatform)
	{
		return ShouldCompileShadersForReferenceMode(InPlatform) ? (uint32)FMath::Clamp<int32>(CVarMegaLightsReferenceShadingPassCount.GetValueOnRenderThread(), 1, 10*1024) : 1u;
	}

	uint32 GetStateFrameIndex(FSceneViewState* ViewState, EShaderPlatform InPlatform)
	{
		uint32 StateFrameIndex = ViewState ? ViewState->GetFrameIndex() : 0;

		if (CVarMegaLightsFixedStateFrameIndex.GetValueOnRenderThread() >= 0)
		{
			StateFrameIndex = CVarMegaLightsFixedStateFrameIndex.GetValueOnRenderThread();
		}

		if (StochasticLighting::IsStateFrameIndexOverridden())
		{
			StateFrameIndex = StochasticLighting::GetStateFrameIndex(ViewState);
		}

		if (CVarMegaLightsReferenceOffsetToStateFrameIndex.GetValueOnRenderThread() > 0)
		{
			StateFrameIndex += CVarMegaLightsReferenceOffsetToStateFrameIndex.GetValueOnRenderThread();
		}

		//In case we accumulate we account for this in the state frame index to get the same property out of the BlueNoise.
		StateFrameIndex *= GetReferenceShadingPassCount(InPlatform);

		return StateFrameIndex;
	}

	FIntPoint GetDownsampleFactorXY(EMegaLightsInput InputType, EShaderPlatform ShaderPlatform)
	{
		uint32 DownsampleMode = 0;

		if (InputType == EMegaLightsInput::GBuffer)
		{
			DownsampleMode = FMath::Clamp(CVarMegaLightsDownsampleMode.GetValueOnAnyThread(), 0, 2);
		}
		else if (InputType == EMegaLightsInput::HairStrands)
		{
			DownsampleMode = FMath::Clamp(CVarMegaLightsHairStrandsDownsampleMode.GetValueOnAnyThread(), 0, 2);
		}
		else
		{
			checkf(false, TEXT("MegaLight::GetDownsampleFactorXY not implemented"));
		}

		FIntPoint DownsampleFactorXY = FIntPoint(1, 1);
		switch (DownsampleMode)
		{
			case 0: DownsampleFactorXY = FIntPoint(1, 1); break;
			case 1: DownsampleFactorXY = FIntPoint(2, 1); break;
			case 2: DownsampleFactorXY = FIntPoint(2, 2); break;
		}

		const bool bReferenceMode = GetReferenceShadingPassCount(ShaderPlatform) > 1u;
		if (bReferenceMode)
		{
			DownsampleFactorXY = FIntPoint(1, 1);
		}

		return DownsampleFactorXY;
	}

	FIntPoint GetDownsampleFactorXY(StochasticLighting::EMaterialSource MaterialSource, EShaderPlatform ShaderPlatform)
	{
		if (MaterialSource == StochasticLighting::EMaterialSource::GBuffer)
		{
			return GetDownsampleFactorXY(EMegaLightsInput::GBuffer, ShaderPlatform);
		}
		else if (MaterialSource == StochasticLighting::EMaterialSource::HairStrands)
		{
			return GetDownsampleFactorXY(EMegaLightsInput::HairStrands, ShaderPlatform);
		}
		else
		{
			return GetDownsampleFactorXY(EMegaLightsInput::Count, ShaderPlatform);
		}
	}

	FIntPoint GetNumSamplesPerPixel2d(int32 NumSamplesPerPixel1d)
	{
		if (NumSamplesPerPixel1d >= 16)
		{
			return FIntPoint(4, 4);
		}
		else if (NumSamplesPerPixel1d >= 4)
		{
			return FIntPoint(2, 2);
		}
		else
		{
			return FIntPoint(2, 1);
		}
	}

	FIntPoint GetNumSamplesPerPixel2d(EMegaLightsInput InputType)
	{
		switch (InputType)
		{
			case EMegaLightsInput::GBuffer: return GetNumSamplesPerPixel2d(CVarMegaLightsNumSamplesPerPixel.GetValueOnAnyThread());
			case EMegaLightsInput::HairStrands: return GetNumSamplesPerPixel2d(CVarMegaLightsNumSamplesPerPixelHairStrands.GetValueOnAnyThread());
			default: checkf(false, TEXT("MegaLight::GetNumSamplesPerPixel2d not implemented")); return false;
		};
	}

	FIntVector GetNumSamplesPerVoxel3d(int32 NumSamplesPerVoxel1d)
	{
		if (NumSamplesPerVoxel1d >= 4)
		{
			return FIntVector(2, 2, 1);
		}
		else
		{
			return FIntVector(2, 1, 1);
		}
	}

	int32 GetVisualizeLightLoopIterationsMode()
	{
		return FMath::Clamp(CVarMegaLightsDebugVisualizeLightLoopIterations.GetValueOnRenderThread(), 0, 2);
	}

	int32 GetDebugMode(EMegaLightsInput InputType)
	{
		if (CVarMegaLightsVolumeDebug.GetValueOnRenderThread() != 0
			|| CVarMegaLightsTranslucencyVolumeDebug.GetValueOnRenderThread() != 0
			// Don't show debug texts when visualizing light loop iteration count
			|| GetVisualizeLightLoopIterationsMode() != 0)
		{
			return 0;
		}
		switch (InputType)
		{
			case EMegaLightsInput::GBuffer: return CVarMegaLightsDebug.GetValueOnRenderThread();
			case EMegaLightsInput::HairStrands: return CVarMegaLightsDebugHairStrands.GetValueOnRenderThread();
		};
		return 0;
	}

	bool IsDebugEnabledForShadingPass(int32 ShadingPassIndex, EShaderPlatform InPlatform)
	{
		int32 NumPass = GetReferenceShadingPassCount(InPlatform);
		int32 DebuggedPassIndex = CVarMegaLightsReferenceDebuggedPassIndex.GetValueOnRenderThread();
		if (DebuggedPassIndex >= 0)
		{
			return ShadingPassIndex == DebuggedPassIndex;
		}
		else
		{
			return ShadingPassIndex == NumPass + DebuggedPassIndex;
		}
	}

	bool SupportsSpatialFilter(EMegaLightsInput InputType)
	{
		switch (InputType)
		{
			case EMegaLightsInput::GBuffer: return true;
			case EMegaLightsInput::HairStrands: return false; // Disable for now due to lack of proper reconstruction filter
			default: checkf(false, TEXT("MegaLight::SupportsSpatialFilter not implemented")); return false;
		};
	}

	bool UseWaveOps(EShaderPlatform ShaderPlatform)
	{
		return CVarMegaLightsWaveOps.GetValueOnRenderThread() != 0
			&& GRHISupportsWaveOperations
			&& RHISupportsWaveOperations(ShaderPlatform);
	}

	void ModifyCompilationEnvironment(EShaderPlatform Platform, FShaderCompilerEnvironment& OutEnvironment)
	{
		FForwardLightingParameters::ModifyCompilationEnvironment(Platform, OutEnvironment);
		ShaderPrint::ModifyCompilationEnvironment(Platform, OutEnvironment);
		OutEnvironment.CompilerFlags.Add(CFLAG_WarningsAsErrors);
	}

	const TCHAR* GetTileTypeString(ETileType TileType)
	{
		switch (TileType)
		{
		case ETileType::SimpleShading:						return TEXT("Simple");
		case ETileType::SingleShading:						return TEXT("Single");
		case ETileType::ComplexShading:						return TEXT("Complex");
		case ETileType::ComplexSpecialShading:				return TEXT("Complex Special ");

		case ETileType::SimpleShading_Rect:					return TEXT("Simple Rect");
		case ETileType::SingleShading_Rect:					return TEXT("Single Rect");
		case ETileType::ComplexShading_Rect:				return TEXT("Complex Rect");
		case ETileType::ComplexSpecialShading_Rect:			return TEXT("Complex Special Rect");

		case ETileType::SimpleShading_Rect_Textured:		return TEXT("Simple Textured Rect");
		case ETileType::SingleShading_Rect_Textured:		return TEXT("Single Textured Rect");
		case ETileType::ComplexShading_Rect_Textured:		return TEXT("Complex Textured Rect");
		case ETileType::ComplexSpecialShading_Rect_Textured:return TEXT("Complex Special Textured Rect");

		case ETileType::Empty:								return TEXT("Empty");
		
		default:
			return nullptr;
		}
	}

	bool IsRectLightTileType(ETileType TileType)
	{
		return TileType == MegaLights::ETileType::SimpleShading_Rect
			|| TileType == MegaLights::ETileType::ComplexShading_Rect
			|| TileType == MegaLights::ETileType::SimpleShading_Rect_Textured
			|| TileType == MegaLights::ETileType::ComplexShading_Rect_Textured

			|| TileType == MegaLights::ETileType::SingleShading_Rect
			|| TileType == MegaLights::ETileType::ComplexSpecialShading_Rect
			|| TileType == MegaLights::ETileType::SingleShading_Rect_Textured
			|| TileType == MegaLights::ETileType::ComplexSpecialShading_Rect_Textured;
	}

	bool IsTexturedLightTileType(ETileType TileType)
	{
		return TileType == MegaLights::ETileType::SimpleShading_Rect_Textured
			|| TileType == MegaLights::ETileType::ComplexShading_Rect_Textured
			|| TileType == MegaLights::ETileType::SingleShading_Rect_Textured
			|| TileType == MegaLights::ETileType::ComplexSpecialShading_Rect_Textured;
	}

	bool IsComplexTileType(ETileType TileType)
	{
		return TileType == MegaLights::ETileType::ComplexShading
			|| TileType == MegaLights::ETileType::ComplexSpecialShading
			|| TileType == MegaLights::ETileType::ComplexShading_Rect
			|| TileType == MegaLights::ETileType::ComplexSpecialShading_Rect
			|| TileType == MegaLights::ETileType::ComplexShading_Rect_Textured
			|| TileType == MegaLights::ETileType::ComplexSpecialShading_Rect_Textured;
	}

	TArray<int32> GetShadingTileTypes(EMegaLightsInput InputType)
	{
		// Build available tile types
		TArray<int32> Out;
		if (InputType == EMegaLightsInput::GBuffer)
		{
			for (int32 TileType = 0; TileType < (int32)MegaLights::ETileType::SHADING_MAX_LEGACY; ++TileType)
			{
				Out.Add(TileType);
			}
			if (Substrate::IsSubstrateEnabled())
			{
				for (int32 TileType = (int32)MegaLights::ETileType::SHADING_MIN_SUBSTRATE; TileType < (int32)MegaLights::ETileType::SHADING_MAX_SUBSTRATE; ++TileType)
				{
					Out.Add(TileType);
				}
			}
		}
		else if (InputType == EMegaLightsInput::HairStrands)
		{
			// Hair only uses complex tiles
			Out.Add(int32(MegaLights::ETileType::ComplexShading));
			Out.Add(int32(MegaLights::ETileType::ComplexShading_Rect));
			Out.Add(int32(MegaLights::ETileType::ComplexShading_Rect_Textured));
		}
		else
		{
			checkf(false, TEXT("MegaLight::GetShadingTileTypes(...) not implemented"))
		}
		return Out;
	}

	void SetupTileClassifyParameters(const FViewInfo& View, MegaLights::FTileClassifyParameters& OutParameters)
	{
		OutParameters.EnableTexturedRectLights = CVarMegaLightsTexturedRectLights.GetValueOnRenderThread();
	}
};

namespace MegaLightsVolume
{
	uint32 GetDownsampleFactor(EShaderPlatform ShaderPlatform)
	{
		const uint32 DownsampleMode = FMath::Clamp(CVarMegaLightsVolumeDownsampleMode.GetValueOnAnyThread(), 0, 2);
		uint32 DownsampleFactor = DownsampleMode == 2 ? 2 : 1;

		const bool bReferenceMode = MegaLights::GetReferenceShadingPassCount(ShaderPlatform) > 1u;
		if (bReferenceMode)
		{
			DownsampleFactor = 1;
		}

		return DownsampleFactor;
	}

	FIntVector GetNumSamplesPerVoxel3d()
	{
		return MegaLights::GetNumSamplesPerVoxel3d(CVarMegaLightsVolumeNumSamplesPerVoxel.GetValueOnAnyThread());
	}

	bool UsesLightFunction()
	{
		return CVarMegaLightsVolumeLightFunctions.GetValueOnRenderThread() != 0;
	}

	int32 GetDebugMode()
	{
		return CVarMegaLightsVolumeDebug.GetValueOnRenderThread();
	}
}

namespace MegaLightsTranslucencyVolume
{
	uint32 GetDownsampleFactor(EShaderPlatform ShaderPlatform)
	{
		uint32 DownsampleFactor = FMath::Clamp(CVarMegaLightsTranslucencyVolumeDownsampleFactor.GetValueOnAnyThread(), 1, 2);
		
		const bool bReferenceMode = MegaLights::GetReferenceShadingPassCount(ShaderPlatform) > 1u;
		if (bReferenceMode)
		{
			DownsampleFactor = 1;
		}

		return DownsampleFactor;
	}

	FIntVector GetNumSamplesPerVoxel3d()
	{
		return MegaLights::GetNumSamplesPerVoxel3d(CVarMegaLightsTranslucencyVolumeNumSamplesPerVoxel.GetValueOnAnyThread());
	}

	bool UsesLightFunction()
	{
		return CVarMegaLightsTranslucencyVolumeLightFunctions.GetValueOnRenderThread() != 0;
	}

	int32 GetDebugMode()
	{
		return CVarMegaLightsTranslucencyVolumeDebug.GetValueOnRenderThread();
	}
}

class FMegaLightsTileClassificationBuildListsCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FMegaLightsTileClassificationBuildListsCS)
	SHADER_USE_PARAMETER_STRUCT(FMegaLightsTileClassificationBuildListsCS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FMegaLightsParameters, MegaLightsParameters)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, RWTileAllocator)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, RWTileData)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, MegaLightsTileBitmask)
		SHADER_PARAMETER(FIntPoint, ViewSizeInTiles)
		SHADER_PARAMETER(FIntPoint, ViewMinInTiles)
		SHADER_PARAMETER(FIntPoint, DownsampledViewSizeInTiles)
		SHADER_PARAMETER(FIntPoint, DownsampledViewMinInTiles)
		SHADER_PARAMETER(uint32, OutputTileDataStride)
	END_SHADER_PARAMETER_STRUCT()

	class FDownsampleFactorX : SHADER_PERMUTATION_RANGE_INT("DOWNSAMPLE_FACTOR_X", 1, 2);
	class FDownsampleFactorY : SHADER_PERMUTATION_RANGE_INT("DOWNSAMPLE_FACTOR_Y", 1, 2);
	using FPermutationDomain = TShaderPermutationDomain<FDownsampleFactorX, FDownsampleFactorY>;

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
		MegaLights::ModifyCompilationEnvironment(Parameters.Platform, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GetGroupSize());
		OutEnvironment.CompilerFlags.Add(CFLAG_WaveOperations);
	}

	static int32 GetGroupSize()
	{
		return 8;
	}
};

IMPLEMENT_GLOBAL_SHADER(FMegaLightsTileClassificationBuildListsCS, "/Engine/Private/MegaLights/MegaLights.usf", "MegaLightsTileClassificationBuildListsCS", SF_Compute);

class FInitTileIndirectArgsCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FInitTileIndirectArgsCS)
	SHADER_USE_PARAMETER_STRUCT(FInitTileIndirectArgsCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FMegaLightsParameters, MegaLightsParameters)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWTileIndirectArgs)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWDownsampledTileIndirectArgs)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, TileAllocator)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, DownsampledTileAllocator)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return MegaLights::ShouldCompileShaders(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GetGroupSize());
	}

	static int32 GetGroupSize()
	{
		return 64;
	}
};

IMPLEMENT_GLOBAL_SHADER(FInitTileIndirectArgsCS, "/Engine/Private/MegaLights/MegaLights.usf", "InitTileIndirectArgsCS", SF_Compute);

DECLARE_GPU_STAT(MegaLights);

extern int32 GetTranslucencyLightingVolumeDim();

static int32 GetVolumeGridPixelSize()
{
	return FMath::Max(1, CVarMegaLightsVolumeGridPixelSize.GetValueOnRenderThread());
}

static int32 GetVolumeGridSizeZ()
{
	return FMath::Max(1, CVarMegaLightsVolumeGridSizeZ.GetValueOnRenderThread());
}

static FVector GetVolumeGridZParams(float VolumeStartDistance, float NearPlane, float FarPlane, int32 GridSizeZ)
{
	// Don't spend lots of resolution right in front of the near plane
	NearPlane = FMath::Max(NearPlane, VolumeStartDistance);

	return CalculateGridZParams(NearPlane, FarPlane, CVarMegaLightsVolumeDepthDistributionScale.GetValueOnRenderThread(), GridSizeZ);
}

static FIntVector GetVolumeGridSize(const FIntPoint& TargetResolution, int32& OutGridPixelSize)
{
	int32 GridPixelSize = GetVolumeGridPixelSize();
	FIntPoint GridSizeXY = FIntPoint::DivideAndRoundUp(TargetResolution, GridPixelSize);
	if (GridSizeXY.X > GMaxVolumeTextureDimensions || GridSizeXY.Y > GMaxVolumeTextureDimensions) //clamp to max volume texture dimensions. only happens for extreme resolutions (~8x2k)
	{
		float PixelSizeX = (float)TargetResolution.X / GMaxVolumeTextureDimensions;
		float PixelSizeY = (float)TargetResolution.Y / GMaxVolumeTextureDimensions;
		GridPixelSize = FMath::Max(FMath::CeilToInt(PixelSizeX), FMath::CeilToInt(PixelSizeY));
		GridSizeXY = FIntPoint::DivideAndRoundUp(TargetResolution, GridPixelSize);
	}
	OutGridPixelSize = GridPixelSize;
	return FIntVector(GridSizeXY.X, GridSizeXY.Y, GetVolumeGridSizeZ());
}

FIntVector GetVolumeResourceGridSize(const FViewInfo& View, int32& OutGridPixelSize)
{
	return GetVolumeGridSize(View.GetSceneTexturesConfig().Extent, OutGridPixelSize);
}

FIntVector GetVolumeViewGridSize(const FViewInfo& View, int32& OutGridPixelSize)
{
	return GetVolumeGridSize(View.ViewRect.Size(), OutGridPixelSize);
}

FVector2f GetVolumeUVMaxForSampling(const FVector2f& ViewRectSize, FIntVector ResourceGridSize, int32 ResourceGridPixelSize)
{
	float ViewRectSizeXSafe = FMath::DivideAndRoundUp<int32>(int32(ViewRectSize.X), ResourceGridPixelSize) * ResourceGridPixelSize - (ResourceGridPixelSize / 2 + 1);
	float ViewRectSizeYSafe = FMath::DivideAndRoundUp<int32>(int32(ViewRectSize.Y), ResourceGridPixelSize) * ResourceGridPixelSize - (ResourceGridPixelSize / 2 + 1);
	return FVector2f(ViewRectSizeXSafe, ViewRectSizeYSafe) / (FVector2f(ResourceGridSize.X, ResourceGridSize.Y) * ResourceGridPixelSize);
}

FVector2f GetVolumePrevUVMaxForTemporalBlend(const FVector2f& ViewRectSize, FIntVector VolumeResourceGridSize, int32 VolumeResourceGridPixelSize)
{
	float ViewRectSizeXSafe = FMath::DivideAndRoundUp<int32>(int32(ViewRectSize.X), VolumeResourceGridPixelSize) * VolumeResourceGridPixelSize;
	float ViewRectSizeYSafe = FMath::DivideAndRoundUp<int32>(int32(ViewRectSize.Y), VolumeResourceGridPixelSize) * VolumeResourceGridPixelSize;
	return FVector2f(ViewRectSizeXSafe, ViewRectSizeYSafe) / (FVector2f(VolumeResourceGridSize.X, VolumeResourceGridSize.Y) * VolumeResourceGridPixelSize);
}

FVector2f GetVolumeFroxelToScreenSVPosRatio(const FViewInfo& View)
{
	const FIntPoint ViewRectSize = View.ViewRect.Size();

	// Calculate how much the Fog froxel volume "overhangs" the actual view frustum to the right and bottom.
	// This needs to be applied on SVPos because froxel pixel size (see r.VolumetricFog.GridPixelSize) does not align perfectly with view rect.
	int32 VolumeGridPixelSize;
	const FIntVector VolumeGridSize = GetVolumeViewGridSize(View, VolumeGridPixelSize);
	const FVector2f FogPhysicalSize = FVector2f(VolumeGridSize.X, VolumeGridSize.Y) * VolumeGridPixelSize;
	const FVector2f ClipRatio = FogPhysicalSize / FVector2f(ViewRectSize);
	return ClipRatio;
}

void SetupMegaLightsVolumeData(const FViewInfo& View, bool bShouldRenderVolumetricFog, bool bShouldRenderTranslucencyVolume, FMegaLightsVolumeData& Parameters)
{
	const FScene* Scene = (FScene*)View.Family->Scene;

	float MaxDistance = 0.0f;

	{
		if (bShouldRenderTranslucencyVolume)
		{
			// Max distance to TLV corner

			FBox TLVOuterBoundingBox(View.TranslucencyLightingVolumeMin[TVC_Outer], View.TranslucencyLightingVolumeMin[TVC_Outer] + View.TranslucencyLightingVolumeSize[TVC_Outer]);

			FVector Vertices[8];
			TLVOuterBoundingBox.GetVertices(Vertices);

			for (const FVector& V : Vertices)
			{
				MaxDistance = FMath::Max(MaxDistance, (float)FVector::Dist(V, View.ViewMatrices.GetViewOrigin()));
			}
		}

		if (bShouldRenderVolumetricFog && Scene->ExponentialFogs.Num() > 0)
		{
			const FExponentialHeightFogSceneInfo& FogInfo = Scene->ExponentialFogs[0];

			MaxDistance = FMath::Max(MaxDistance, FogInfo.VolumetricFogDistance);
		}
	}

	int32 VolumeGridPixelSize;
	const FIntVector VolumeViewGridSize = GetVolumeViewGridSize(View, VolumeGridPixelSize);
	const FIntVector VolumeResourceGridSize = GetVolumeResourceGridSize(View, VolumeGridPixelSize);

	Parameters.ViewGridSizeInt = VolumeViewGridSize;
	Parameters.ViewGridSize = FVector3f(VolumeViewGridSize);
	Parameters.ResourceGridSizeInt = VolumeResourceGridSize;
	Parameters.ResourceGridSize = FVector3f(VolumeResourceGridSize);

	FVector ZParams = GetVolumeGridZParams(0, View.NearClippingDistance, MaxDistance, VolumeResourceGridSize.Z);
	Parameters.GridZParams = (FVector3f)ZParams;

	Parameters.SVPosToVolumeUV = FVector2f::UnitVector / (FVector2f(VolumeResourceGridSize.X, VolumeResourceGridSize.Y) * VolumeGridPixelSize);
	Parameters.FogGridToPixelXY = FIntPoint(VolumeGridPixelSize, VolumeGridPixelSize);
	Parameters.MaxDistance = MaxDistance;
}

FMegaLightsViewContext::FMegaLightsViewContext(
	FRDGBuilder& InGraphBuilder,
	const int32 InViewIndex,
	const FViewInfo& InView,
	const FSceneViewFamily& InViewFamily,
	const FScene* InScene,
	const FSceneTextures& InSceneTextures,
	bool bInUseVSM)
	: GraphBuilder(InGraphBuilder)
	, ViewIndex(InViewIndex)
	, View(InView)
	, ViewFamily(InViewFamily)
	, Scene(InScene)
	, SceneTextures(InSceneTextures)
	, bUseVSM(bInUseVSM)
{
	StochasticLighting::InitDefaultHistoryScreenParameters(HistoryScreenParameters);
}

FRDGTextureRef FMegaLightsViewContext::TileClassificationMark(uint32 ShadingPassIndex)
{
	const FIntPoint BufferSizeInTiles = FMath::DivideAndRoundUp<FIntPoint>(SceneTextures.Config.Extent, MegaLights::TileSize);

	FLumenFrontLayerTranslucencyGBufferParameters FrontLayerTranslucencyGBuffer;
	FrontLayerTranslucencyGBuffer.FrontLayerTranslucencyNormal = nullptr;
	FrontLayerTranslucencyGBuffer.FrontLayerTranslucencySceneDepth = nullptr;

	StochasticLighting::EMaterialSource MaterialSource;
	switch (InputType)
	{
	case EMegaLightsInput::HairStrands:
		MaterialSource = StochasticLighting::EMaterialSource::HairStrands;
		break;
	default:
		checkNoEntry();
	case EMegaLightsInput::GBuffer:
		MaterialSource = StochasticLighting::EMaterialSource::GBuffer;
		break;
	}

	FRDGTextureRef MegaLightsTileBitmask = GraphBuilder.CreateTexture(
		FRDGTextureDesc::Create2D(BufferSizeInTiles, PF_R8_UINT, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV),
		TEXT("MegaLights.TileBitmask"));

	FRDGTextureUAVRef DepthHistoryUAV;
	FRDGTextureUAVRef NormalHistoryUAV;
	int32 StateFrameIndexOverride;
	if (ShadingPassIndex == 0)
	{
		DepthHistoryUAV = GraphBuilder.CreateUAV(SceneDepth);
		NormalHistoryUAV = GraphBuilder.CreateUAV(SceneWorldNormal);
		StateFrameIndexOverride = -1;
	}
	else
	{
		DepthHistoryUAV = nullptr;
		NormalHistoryUAV = nullptr;
		StateFrameIndexOverride = FirstPassStateFrameIndex + ShadingPassIndex;
	}

	FRDGTextureUAVRef DownsampledSceneDepth2x1UAV = nullptr;
	FRDGTextureUAVRef DownsampledWorldNormal2x1UAV = nullptr;
	FRDGTextureUAVRef DownsampledSceneDepth2x2UAV = nullptr;
	FRDGTextureUAVRef DownsampledWorldNormal2x2UAV = nullptr;
	if (DownsampleFactor == FIntPoint(2, 1))
	{
		DownsampledSceneDepth2x1UAV = GraphBuilder.CreateUAV(DownsampledSceneDepth);
		DownsampledWorldNormal2x1UAV = GraphBuilder.CreateUAV(DownsampledSceneWorldNormal);
	}
	else if (DownsampleFactor == FIntPoint(2, 2))
	{
		DownsampledSceneDepth2x2UAV = GraphBuilder.CreateUAV(DownsampledSceneDepth);
		DownsampledWorldNormal2x2UAV = GraphBuilder.CreateUAV(DownsampledSceneWorldNormal);
	}

	StochasticLighting::FRunConfig RunConfig;
	RunConfig.StateFrameIndexOverride = StateFrameIndexOverride;
	RunConfig.bCopyDepthAndNormal = DepthHistoryUAV != nullptr;
	RunConfig.bDownsampleDepthAndNormal2x1 = DownsampledSceneDepth2x1UAV != nullptr;
	RunConfig.bDownsampleDepthAndNormal2x2 = DownsampledSceneDepth2x2UAV != nullptr;
	RunConfig.bTileClassifyMegaLights = true;
	RunConfig.bReprojectMegaLights = true;

	StochasticLighting::FContext StochasticLightingContext(GraphBuilder, SceneTextures, FrontLayerTranslucencyGBuffer, MaterialSource);
	StochasticLightingContext.DepthHistoryUAV = DepthHistoryUAV;
	StochasticLightingContext.NormalHistoryUAV = NormalHistoryUAV;
	StochasticLightingContext.DownsampledSceneDepth2x1UAV = DownsampledSceneDepth2x1UAV;
	StochasticLightingContext.DownsampledWorldNormal2x1UAV = DownsampledWorldNormal2x1UAV;
	StochasticLightingContext.DownsampledSceneDepth2x2UAV = DownsampledSceneDepth2x2UAV;
	StochasticLightingContext.DownsampledWorldNormal2x2UAV = DownsampledWorldNormal2x2UAV;
	StochasticLightingContext.MegaLightsTileBitmaskUAV = GraphBuilder.CreateUAV(MegaLightsTileBitmask);
	StochasticLightingContext.EncodedHistoryScreenCoordUAV = GraphBuilder.CreateUAV(HistoryScreenParameters.EncodedHistoryScreenCoordTexture);
	StochasticLightingContext.MegaLightsPackedPixelDataUAV = GraphBuilder.CreateUAV(HistoryScreenParameters.PackedPixelDataTexture);
	StochasticLightingContext.OutHistoryScreenParameters = &HistoryScreenParameters;

	StochasticLightingContext.Run(View, EReflectionsMethod::Disabled, RunConfig);

	return MegaLightsTileBitmask;
}

void FMegaLightsViewContext::Setup(
	FRDGTextureRef LightingChannelsTexture,
	const FLumenSceneFrameTemporaries& LumenFrameTemporaries,
	const bool bInShouldRenderVolumetricFog,
	const bool bInShouldRenderTranslucencyVolume,
	TUniformBufferRef<FBlueNoise> BlueNoiseUniformBuffer, 
	EMegaLightsInput InInputType)
{
	// History reset for debugging purposes
	bool bResetHistory = false;

	if (GMegaLightsResetEveryNthFrame > 0 && (ViewFamily.FrameNumber % (uint32)GMegaLightsResetEveryNthFrame) == 0)
	{
		bResetHistory = true;
	}

	if (GMegaLightsReset != 0)
	{
		GMegaLightsReset = 0;
		bResetHistory = true;
	}

	InputType = InInputType;

	bShouldRenderVolumetricFog = bInShouldRenderVolumetricFog;
	bShouldRenderTranslucencyVolume = bInShouldRenderTranslucencyVolume;

	bUnifiedVolume = MegaLights::UseVolume() && CVarMegaLightsVolumeUnified.GetValueOnRenderThread() != 0;
	bVolumeEnabled = MegaLights::UseVolume() && (bShouldRenderVolumetricFog || (bUnifiedVolume && bShouldRenderTranslucencyVolume));

	bDebug = MegaLights::GetDebugMode(InputType) != 0;
	bVolumeDebug = MegaLightsVolume::GetDebugMode() != 0;
	bTranslucencyVolumeDebug = MegaLightsTranslucencyVolume::GetDebugMode() != 0;
	DebugTileClassificationMode = CVarMegaLightsDebugTileClassification.GetValueOnRenderThread();
	VisualizeLightLoopIterationsMode = MegaLights::GetVisualizeLightLoopIterationsMode();

	NumSamplesPerPixel2d = MegaLights::GetNumSamplesPerPixel2d(InputType);
	NumSamplesPerVoxel3d = MegaLightsVolume::GetNumSamplesPerVoxel3d();
	NumSamplesPerTranslucencyVoxel3d = MegaLightsTranslucencyVolume::GetNumSamplesPerVoxel3d();

	DownsampleFactor = MegaLights::GetDownsampleFactorXY(InputType, View.GetShaderPlatform());
		const FIntPoint DownsampledViewSize = FIntPoint::DivideAndRoundUp(View.ViewRect.Size(), DownsampleFactor);
		const FIntPoint SampleViewSize = DownsampledViewSize * NumSamplesPerPixel2d;
		const FIntPoint DownsampledBufferSize = FIntPoint::DivideAndRoundUp(SceneTextures.Config.Extent, DownsampleFactor);
	SampleBufferSize = DownsampledBufferSize * NumSamplesPerPixel2d;
	DonwnsampledSampleBufferSize = DownsampledBufferSize * NumSamplesPerPixel2d;

	DownsampledSceneDepth = GraphBuilder.CreateTexture(
			FRDGTextureDesc::Create2D(DownsampledBufferSize, PF_R32_FLOAT, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV),
			TEXT("MegaLights.DownsampledSceneDepth"));

	DownsampledSceneWorldNormal = GraphBuilder.CreateTexture(
			FRDGTextureDesc::Create2D(DownsampledBufferSize, PF_A2B10G10R10, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV),
			TEXT("MegaLights.DownsampledSceneWorldNormal"));

	LightSamples = GraphBuilder.CreateTexture(
			FRDGTextureDesc::Create2D(DonwnsampledSampleBufferSize, PF_R32_UINT, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV),
			TEXT("MegaLights.LightSamples"));

	LightSampleRays = GraphBuilder.CreateTexture(
			FRDGTextureDesc::Create2D(DonwnsampledSampleBufferSize, PF_R32_UINT, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV),
			TEXT("MegaLights.LightSampleRays"));

	bSpatial  = MegaLights::SupportsSpatialFilter(InputType) && MegaLights::UseSpatialFilter();
	bTemporal = MegaLights::UseTemporalFilter();

	VisibleLightHashSizeInTiles = FMath::DivideAndRoundUp<FIntPoint>(SceneTextures.Config.Extent, MegaLights::VisibleLightHashTileSize);
	VisibleLightHashViewMinInTiles = FMath::DivideAndRoundUp<FIntPoint>(View.ViewRect.Min, MegaLights::VisibleLightHashTileSize);
	VisibleLightHashViewSizeInTiles = FMath::DivideAndRoundUp<FIntPoint>(View.ViewRect.Size(), MegaLights::VisibleLightHashTileSize);
	VisibleLightHashBufferSize = VisibleLightHashSizeInTiles.X * VisibleLightHashSizeInTiles.Y * MegaLights::VisibleLightHashSize;

	SetupMegaLightsVolumeData(View, bShouldRenderVolumetricFog, bShouldRenderTranslucencyVolume, VolumeParameters);

	if (bShouldRenderVolumetricFog)
	{
		SetupVolumetricFogGlobalData(View, VolumetricFogParamaters);
	}

	if (!bUnifiedVolume)
	{
		VolumeParameters.ViewGridSizeInt = VolumetricFogParamaters.ViewGridSizeInt;
		VolumeParameters.ViewGridSize = VolumetricFogParamaters.ViewGridSize;
		VolumeParameters.ResourceGridSizeInt = VolumetricFogParamaters.ResourceGridSizeInt;
		VolumeParameters.ResourceGridSize = VolumetricFogParamaters.ResourceGridSize;
		VolumeParameters.GridZParams = VolumetricFogParamaters.GridZParams;
		VolumeParameters.SVPosToVolumeUV = VolumetricFogParamaters.SVPosToVolumeUV;
		VolumeParameters.FogGridToPixelXY = VolumetricFogParamaters.FogGridToPixelXY;
		VolumeParameters.MaxDistance = VolumetricFogParamaters.MaxDistance;
	}

	VolumeDownsampleFactor = MegaLightsVolume::GetDownsampleFactor(View.GetShaderPlatform());
	VolumeViewSize = VolumeParameters.ViewGridSizeInt;
	VolumeBufferSize = VolumeParameters.ResourceGridSizeInt;
		const FIntVector VolumeDownsampledBufferSize = FIntVector::DivideAndRoundUp(VolumeParameters.ResourceGridSizeInt, VolumeDownsampleFactor);
	VolumeDownsampledViewSize = FIntVector::DivideAndRoundUp(VolumeParameters.ViewGridSizeInt, VolumeDownsampleFactor);
		const FIntVector VolumeSampleViewSize = VolumeDownsampledViewSize * NumSamplesPerVoxel3d;
	VolumeSampleBufferSize = VolumeDownsampledBufferSize * NumSamplesPerVoxel3d;

	VolumeVisibleLightHashTileSize = FIntVector(4, 4, 2);

	VolumeVisibleLightHashSizeInTiles = FIntVector(
			FMath::DivideAndRoundUp(VolumeBufferSize.X, VolumeVisibleLightHashTileSize.X),
			FMath::DivideAndRoundUp(VolumeBufferSize.Y, VolumeVisibleLightHashTileSize.Y),
			FMath::DivideAndRoundUp(VolumeBufferSize.Z, VolumeVisibleLightHashTileSize.Z));
	VolumeVisibleLightHashViewSizeInTiles = FIntVector(
			FMath::DivideAndRoundUp(VolumeViewSize.X, VolumeVisibleLightHashTileSize.X),
			FMath::DivideAndRoundUp(VolumeViewSize.Y, VolumeVisibleLightHashTileSize.Y),
			FMath::DivideAndRoundUp(VolumeViewSize.Z, VolumeVisibleLightHashTileSize.Z));
	VolumeVisibleLightHashBufferSize = VolumeVisibleLightHashSizeInTiles.X * VolumeVisibleLightHashSizeInTiles.Y * VolumeVisibleLightHashSizeInTiles.Z * MegaLights::VisibleLightHashSize;

	TranslucencyVolumeDownsampleFactor = bUnifiedVolume ? VolumeDownsampleFactor : MegaLightsTranslucencyVolume::GetDownsampleFactor(View.GetShaderPlatform());
	TranslucencyVolumeBufferSize = FIntVector(GetTranslucencyLightingVolumeDim());
	TranslucencyVolumeDownsampledBufferSize = bUnifiedVolume ? VolumeDownsampledBufferSize : FIntVector::DivideAndRoundUp(TranslucencyVolumeBufferSize, TranslucencyVolumeDownsampleFactor);
		const FIntVector TranslucencyVolumeDownsampledViewSize = bUnifiedVolume ? VolumeDownsampledViewSize : TranslucencyVolumeDownsampledBufferSize;
	TranslucencyVolumeSampleBufferSize = bUnifiedVolume ? VolumeSampleBufferSize : TranslucencyVolumeDownsampledBufferSize * NumSamplesPerTranslucencyVoxel3d;

	TranslucencyVolumeVisibleLightHashTileSize = FIntVector(2, 2, 2);

	TranslucencyVolumeVisibleLightHashSizeInTiles = FIntVector(
			FMath::DivideAndRoundUp(TranslucencyVolumeBufferSize.X, TranslucencyVolumeVisibleLightHashTileSize.X),
			FMath::DivideAndRoundUp(TranslucencyVolumeBufferSize.Y, TranslucencyVolumeVisibleLightHashTileSize.Y),
			FMath::DivideAndRoundUp(TranslucencyVolumeBufferSize.Z, TranslucencyVolumeVisibleLightHashTileSize.Z));
	TranslucencyVolumeVisibleLightHashBufferSize =
			TranslucencyVolumeVisibleLightHashSizeInTiles.X *
			TranslucencyVolumeVisibleLightHashSizeInTiles.Y *
			TranslucencyVolumeVisibleLightHashSizeInTiles.Z *
			MegaLights::VisibleLightHashSize;

	bGuideByHistory = CVarMegaLightsGuideByHistory.GetValueOnRenderThread() != 0;
	bGuideAreaLightsByHistory = CVarMegaLightsGuideByHistory.GetValueOnRenderThread() == 2;
	bVolumeGuideByHistory = CVarMegaLightsVolumeGuideByHistory.GetValueOnRenderThread() != 0;
	bTranslucencyVolumeGuideByHistory = CVarMegaLightsTranslucencyVolumeGuideByHistory.GetValueOnRenderThread() != 0;
	bSubPixelShading = CVarMegaLightsHairStrandsSubPixelShading.GetValueOnRenderThread() > 0;

	if (View.ViewState)
	{
		const FMegaLightsViewState::FResources& MegaLightsViewState = InputType == EMegaLightsInput::HairStrands ? View.ViewState->MegaLights.HairStrands : View.ViewState->MegaLights.GBuffer;
		const FStochasticLightingViewState& StochasticLightingViewState = View.ViewState->StochasticLighting;

		if (!View.bCameraCut 
			&& !View.bPrevTransformsReset
			&& !bResetHistory)
		{
			HistoryVisibleLightHashViewMinInTiles = MegaLightsViewState.HistoryVisibleLightHashViewMinInTiles;
			HistoryVisibleLightHashViewSizeInTiles = MegaLightsViewState.HistoryVisibleLightHashViewSizeInTiles;

			HistoryVolumeVisibleLightHashViewSizeInTiles = MegaLightsViewState.HistoryVolumeVisibleLightHashViewSizeInTiles;
			HistoryTranslucencyVolumeVisibleLightHashSizeInTiles = MegaLightsViewState.HistoryTranslucencyVolumeVisibleLightHashSizeInTiles;

			if (InputType == EMegaLightsInput::HairStrands)
			{
				if (MegaLightsViewState.SceneDepthHistory)
				{
					SceneDepthHistory = GraphBuilder.RegisterExternalTexture(MegaLightsViewState.SceneDepthHistory);
				}

				if (MegaLightsViewState.SceneNormalHistory)
				{
					SceneNormalAndShadingHistory = GraphBuilder.RegisterExternalTexture(MegaLightsViewState.SceneNormalHistory);
				}
			}
			else
			{
				if (StochasticLightingViewState.SceneDepthHistory)
				{
					SceneDepthHistory = GraphBuilder.RegisterExternalTexture(StochasticLightingViewState.SceneDepthHistory);
				}

				if (StochasticLightingViewState.SceneNormalHistory)
				{
					SceneNormalAndShadingHistory = GraphBuilder.RegisterExternalTexture(StochasticLightingViewState.SceneNormalHistory);
				}
			}

			if (bTemporal &&
				MegaLightsViewState.DiffuseLightingHistory
				&& MegaLightsViewState.SpecularLightingHistory
				&& MegaLightsViewState.LightingMomentsHistory
				&& MegaLightsViewState.NumFramesAccumulatedHistory)
			{
				DiffuseLightingHistory = GraphBuilder.RegisterExternalTexture(MegaLightsViewState.DiffuseLightingHistory);
				SpecularLightingHistory = GraphBuilder.RegisterExternalTexture(MegaLightsViewState.SpecularLightingHistory);
				LightingMomentsHistory = GraphBuilder.RegisterExternalTexture(MegaLightsViewState.LightingMomentsHistory);
				NumFramesAccumulatedHistory = GraphBuilder.RegisterExternalTexture(MegaLightsViewState.NumFramesAccumulatedHistory);
			}

			if (bGuideByHistory
				&& MegaLightsViewState.VisibleLightHashHistory
				&& MegaLightsViewState.VisibleLightMaskHashHistory)
			{
				VisibleLightHashHistory = GraphBuilder.RegisterExternalBuffer(MegaLightsViewState.VisibleLightHashHistory);
				VisibleLightMaskHashHistory = GraphBuilder.RegisterExternalBuffer(MegaLightsViewState.VisibleLightMaskHashHistory);
			}

			if (bVolumeGuideByHistory
				&& MegaLightsViewState.VolumeVisibleLightHashHistory)
			{
				VolumeVisibleLightHashHistory = GraphBuilder.RegisterExternalBuffer(MegaLightsViewState.VolumeVisibleLightHashHistory);
			}

			if (bTranslucencyVolumeGuideByHistory
				&& MegaLightsViewState.TranslucencyVolume0VisibleLightHashHistory
				&& MegaLightsViewState.TranslucencyVolume1VisibleLightHashHistory
				&& TranslucencyVolumeVisibleLightHashBufferSize == MegaLightsViewState.TranslucencyVolume0VisibleLightHashHistory->GetSize() / sizeof(uint32)
				&& TranslucencyVolumeVisibleLightHashBufferSize == MegaLightsViewState.TranslucencyVolume1VisibleLightHashHistory->GetSize() / sizeof(uint32))
			{
				TranslucencyVolumeVisibleLightHashHistory[0] = GraphBuilder.RegisterExternalBuffer(MegaLightsViewState.TranslucencyVolume0VisibleLightHashHistory);
				TranslucencyVolumeVisibleLightHashHistory[1] = GraphBuilder.RegisterExternalBuffer(MegaLightsViewState.TranslucencyVolume1VisibleLightHashHistory);
			}
		}
	}

	// Setup the light function atlas
	bUseLightFunctionAtlas = LightFunctionAtlas::IsEnabled(View, LightFunctionAtlas::ELightFunctionAtlasSystem::MegaLights);

	ViewSizeInTiles = FIntPoint::DivideAndRoundUp(View.ViewRect.Size(), MegaLights::TileSize);
	const int32 TileDataStride = ViewSizeInTiles.X * ViewSizeInTiles.Y;

	const FIntPoint DownsampledViewSizeInTiles = FIntPoint::DivideAndRoundUp(DownsampledViewSize, MegaLights::TileSize);
	const int32 DownsampledTileDataStride = DownsampledViewSizeInTiles.X * DownsampledViewSizeInTiles.Y;

	{
		// Defaults to -2 to avoid selecting simple lights whose LightIds are -1
		const int32 InvalidDebugLightId = INDEX_NONE - 1;

		MegaLightsParameters.ViewUniformBuffer = View.ViewUniformBuffer;
		MegaLightsParameters.Scene = View.GetSceneUniforms().GetBuffer(GraphBuilder);
		MegaLightsParameters.SceneTextures = GetSceneTextureParameters(GraphBuilder, SceneTextures.UniformBuffer);
		MegaLightsParameters.SceneTexturesStruct = SceneTextures.UniformBuffer;
		MegaLightsParameters.Substrate = Substrate::BindSubstrateGlobalUniformParameters(View);
		MegaLightsParameters.HairStrands = HairStrands::BindHairStrandsViewUniformParameters(View);
		MegaLightsParameters.ForwardLightStruct = View.ForwardLightingResources.ForwardLightUniformBuffer;
		MegaLightsParameters.LightFunctionAtlas = LightFunctionAtlas::BindGlobalParameters(GraphBuilder, View);
		MegaLightsParameters.LightingChannelParameters = GetSceneLightingChannelParameters(GraphBuilder, View, LightingChannelsTexture);
		MegaLightsParameters.BlueNoise = BlueNoiseUniformBuffer;
		MegaLightsParameters.PreIntegratedGF = GSystemTextures.PreintegratedGF->GetRHI();
		MegaLightsParameters.PreIntegratedGFSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		MegaLightsParameters.UnjitteredClipToTranslatedWorld = FMatrix44f(View.ViewMatrices.ComputeInvProjectionNoAAMatrix() * View.ViewMatrices.GetTranslatedViewMatrix().GetTransposed()); // LWC_TODO: Precision loss?
		MegaLightsParameters.UnjitteredTranslatedWorldToClip = FMatrix44f(View.ViewMatrices.GetTranslatedViewMatrix() * View.ViewMatrices.ComputeProjectionNoAAMatrix());
		MegaLightsParameters.UnjitteredPrevTranslatedWorldToClip = FMatrix44f(FTranslationMatrix(-View.ViewMatrices.GetPreViewTranslation()) * View.PrevViewInfo.ViewMatrices.GetViewMatrix() * View.PrevViewInfo.ViewMatrices.ComputeProjectionNoAAMatrix());

		MegaLightsParameters.DownsampledViewMin = FIntPoint::DivideAndRoundUp(View.ViewRect.Min, DownsampleFactor);
		MegaLightsParameters.DownsampledViewSize = DownsampledViewSize;
		MegaLightsParameters.SampleViewMin = FIntPoint::DivideAndRoundUp(View.ViewRect.Min, DownsampleFactor) * NumSamplesPerPixel2d;
		MegaLightsParameters.SampleViewSize = SampleViewSize;
		MegaLightsParameters.DownsampleFactor = DownsampleFactor;
		MegaLightsParameters.NumSamplesPerPixel = NumSamplesPerPixel2d;
		MegaLightsParameters.NumSamplesPerPixelDivideShift.X = FMath::FloorLog2(NumSamplesPerPixel2d.X);
		MegaLightsParameters.NumSamplesPerPixelDivideShift.Y = FMath::FloorLog2(NumSamplesPerPixel2d.Y);
		MegaLightsParameters.MegaLightsStateFrameIndex = MegaLights::GetStateFrameIndex(View.ViewState, View.GetShaderPlatform());
		MegaLightsParameters.StochasticLightingStateFrameIndex = StochasticLighting::GetStateFrameIndex(View.ViewState);
		MegaLightsParameters.DownsampledSceneDepth = DownsampledSceneDepth;
		MegaLightsParameters.DownsampledSceneWorldNormal = DownsampledSceneWorldNormal;
		MegaLightsParameters.DownsampledBufferInvSize = FVector2f(1.0f) / DownsampledBufferSize;
		MegaLightsParameters.MinSampleWeight = FMath::Max(CVarMegaLightsMinSampleWeight.GetValueOnRenderThread(), 0.0f);
		MegaLightsParameters.MaxShadingWeight = FMath::Max(CVarMegaLightsMaxShadingWeight.GetValueOnRenderThread(), 0.0f);
		MegaLightsParameters.TileDataStride = TileDataStride;
		MegaLightsParameters.DownsampledTileDataStride = DownsampledTileDataStride;
		MegaLightsParameters.DebugCursorPosition.X = CVarMegaLightsDebugCursorX.GetValueOnRenderThread();
		MegaLightsParameters.DebugCursorPosition.Y = CVarMegaLightsDebugCursorY.GetValueOnRenderThread();
		MegaLightsParameters.DebugMode = MegaLights::GetDebugMode(InputType);
		MegaLightsParameters.DebugLightId = InvalidDebugLightId;
		MegaLightsParameters.DebugVisualizeLight = CVarMegaLightsDebugVisualizeLight.GetValueOnRenderThread();
		MegaLightsParameters.UseIESProfiles = CVarMegaLightsIESProfiles.GetValueOnRenderThread() != 0;
		MegaLightsParameters.UseLightFunctionAtlas = bUseLightFunctionAtlas;

		// If editor is disabled then we don't have a valid cursor position and have to force it to the center of the screen
		if (!GIsEditor && (MegaLightsParameters.DebugCursorPosition.X < 0 || MegaLightsParameters.DebugCursorPosition.Y < 0))
		{		
			MegaLightsParameters.DebugCursorPosition.X = View.ViewRect.Min.X + View.ViewRect.Width() / 2;
			MegaLightsParameters.DebugCursorPosition.Y = View.ViewRect.Min.Y + View.ViewRect.Height() / 2;
		}

		// screen traces use ClosestHZB, volume sampling/shading uses FurthestHZB
		MegaLightsParameters.HZBParameters = GetHZBParameters(GraphBuilder, View, EHZBType::All);
		MegaLightsParameters.VisibleLightHashViewMinInTiles = VisibleLightHashViewMinInTiles;
		MegaLightsParameters.VisibleLightHashViewSizeInTiles = VisibleLightHashViewSizeInTiles;

		if (bDebug || bVolumeDebug || bTranslucencyVolumeDebug || DebugTileClassificationMode != 0 || VisualizeLightLoopIterationsMode != 0)
		{
			const FIntPoint TileCountXY = FIntPoint::DivideAndRoundUp(View.ViewRect.Size(), MegaLights::TileSize);
			const uint32 TileCount = TileCountXY.X * TileCountXY.Y;

			ShaderPrint::SetEnabled(true);
			ShaderPrint::RequestSpaceForLines(4096u + TileCount * 4u);
			ShaderPrint::RequestSpaceForTriangles(TileCount * 2u);
			ShaderPrint::SetParameters(GraphBuilder, View.ShaderPrintData, MegaLightsParameters.ShaderPrintUniformBuffer);

			MegaLightsParameters.DebugLightId = CVarMegaLightsDebugLightId.GetValueOnRenderThread();

			if (MegaLightsParameters.DebugLightId < 0)
			{
				for (auto LightIt = Scene->Lights.CreateConstIterator(); LightIt; ++LightIt)
				{
					const FLightSceneInfoCompact& LightSceneInfoCompact = *LightIt;
					const FLightSceneInfo* const LightSceneInfo = LightSceneInfoCompact.LightSceneInfo;

					if (LightSceneInfo->Proxy->IsSelected())
					{
						MegaLightsParameters.DebugLightId = LightSceneInfo->Id;
						break;
					}
				}

				if (MegaLightsParameters.DebugLightId < 0)
				{
					MegaLightsParameters.DebugLightId = InvalidDebugLightId;
				}
			}
		}
	}

	{
		extern float GInverseSquaredLightDistanceBiasScale;
		MegaLightsVolumeParameters.VolumeMinSampleWeight = FMath::Max(CVarMegaLightsVolumeMinSampleWeight.GetValueOnRenderThread(), 0.0f);
		MegaLightsVolumeParameters.VolumeMaxShadingWeight = FMath::Max(CVarMegaLightsVolumeMaxShadingWeight.GetValueOnRenderThread(), 0.0f);
		MegaLightsVolumeParameters.VolumeDownsampleFactorMultShift = FMath::FloorLog2(VolumeDownsampleFactor);
		MegaLightsVolumeParameters.NumSamplesPerVoxel = NumSamplesPerVoxel3d;
		MegaLightsVolumeParameters.NumSamplesPerVoxelDivideShift.X = FMath::FloorLog2(NumSamplesPerVoxel3d.X);
		MegaLightsVolumeParameters.NumSamplesPerVoxelDivideShift.Y = FMath::FloorLog2(NumSamplesPerVoxel3d.Y);
		MegaLightsVolumeParameters.NumSamplesPerVoxelDivideShift.Z = FMath::FloorLog2(NumSamplesPerVoxel3d.Z);
		MegaLightsVolumeParameters.DownsampledVolumeViewSize = VolumeDownsampledViewSize;
		MegaLightsVolumeParameters.VolumeViewSize = VolumeViewSize;
		MegaLightsVolumeParameters.VolumeSampleViewSize = VolumeSampleViewSize;
		MegaLightsVolumeParameters.VolumeInvBufferSize = FVector3f(1.0f / VolumeBufferSize.X, 1.0f / VolumeBufferSize.Y, 1.0f / VolumeBufferSize.Z);
		MegaLightsVolumeParameters.MegaLightsVolumeZParams = VolumeParameters.GridZParams;
		MegaLightsVolumeParameters.MegaLightsVolumePixelSize = VolumeParameters.FogGridToPixelXY.X;
		MegaLightsVolumeParameters.VolumePhaseG = Scene->ExponentialFogs.Num() > 0 ? Scene->ExponentialFogs[0].VolumetricFogScatteringDistribution : 0.0f;
		MegaLightsVolumeParameters.VolumeInverseSquaredLightDistanceBiasScale = GInverseSquaredLightDistanceBiasScale;
		MegaLightsVolumeParameters.VolumeFrameJitterOffset = VolumetricFogTemporalRandom(View.Family->FrameNumber);
		MegaLightsVolumeParameters.UseHZBOcclusionTest = CVarMegaLightsVolumeHZBOcclusionTest.GetValueOnRenderThread();
		MegaLightsVolumeParameters.VolumeDebugMode = MegaLightsVolume::GetDebugMode();
		MegaLightsVolumeParameters.VolumeDebugSliceIndex = CVarMegaLightsVolumeDebugSliceIndex.GetValueOnRenderThread();
		MegaLightsVolumeParameters.LightSoftFading = GetVolumetricFogLightSoftFading();
		MegaLightsVolumeParameters.TranslucencyVolumeCascadeIndex = 0;
		MegaLightsVolumeParameters.TranslucencyVolumeInvResolution = 0.0f;
		MegaLightsVolumeParameters.IsUnifiedVolume = bUnifiedVolume ? 1 : 0;
		MegaLightsVolumeParameters.ResampleVolumeViewSize = VolumeViewSize;
		MegaLightsVolumeParameters.ResampleVolumeInvBufferSize = FVector3f(1.0f / VolumeBufferSize.X, 1.0f / VolumeBufferSize.Y, 1.0f / VolumeBufferSize.Z);;
		MegaLightsVolumeParameters.ResampleVolumeZParams = VolumeParameters.GridZParams;
	}

	{
		MegaLightsTranslucencyVolumeParameters.VolumeMinSampleWeight = FMath::Max(CVarMegaLightsTranslucencyVolumeMinSampleWeight.GetValueOnRenderThread(), 0.0f);
		MegaLightsTranslucencyVolumeParameters.VolumeMaxShadingWeight = FMath::Max(CVarMegaLightsTranslucencyVolumeMaxShadingWeight.GetValueOnRenderThread(), 0.0f);
		MegaLightsTranslucencyVolumeParameters.VolumeDownsampleFactorMultShift = FMath::FloorLog2(TranslucencyVolumeDownsampleFactor);
		MegaLightsTranslucencyVolumeParameters.NumSamplesPerVoxel = NumSamplesPerTranslucencyVoxel3d;
		MegaLightsTranslucencyVolumeParameters.NumSamplesPerVoxelDivideShift.X = FMath::FloorLog2(NumSamplesPerTranslucencyVoxel3d.X);
		MegaLightsTranslucencyVolumeParameters.NumSamplesPerVoxelDivideShift.Y = FMath::FloorLog2(NumSamplesPerTranslucencyVoxel3d.Y);
		MegaLightsTranslucencyVolumeParameters.NumSamplesPerVoxelDivideShift.Z = FMath::FloorLog2(NumSamplesPerTranslucencyVoxel3d.Z);
		MegaLightsTranslucencyVolumeParameters.DownsampledVolumeViewSize = TranslucencyVolumeDownsampledViewSize;
		MegaLightsTranslucencyVolumeParameters.VolumeViewSize = TranslucencyVolumeBufferSize;
		MegaLightsTranslucencyVolumeParameters.VolumeSampleViewSize = TranslucencyVolumeSampleBufferSize;
		MegaLightsTranslucencyVolumeParameters.VolumeInvBufferSize = FVector3f(1.0f / VolumeBufferSize.X, 1.0f / VolumeBufferSize.Y, 1.0f / VolumeBufferSize.Z);
		MegaLightsTranslucencyVolumeParameters.MegaLightsVolumeZParams = FVector3f::ZeroVector;
		MegaLightsTranslucencyVolumeParameters.MegaLightsVolumePixelSize = 0;
		MegaLightsTranslucencyVolumeParameters.VolumePhaseG = 0.0f;
		MegaLightsTranslucencyVolumeParameters.VolumeInverseSquaredLightDistanceBiasScale = 1.0f;
		MegaLightsTranslucencyVolumeParameters.VolumeFrameJitterOffset = FVector3f::ZeroVector;
		MegaLightsTranslucencyVolumeParameters.UseHZBOcclusionTest = false;
		MegaLightsTranslucencyVolumeParameters.VolumeDebugMode = MegaLightsTranslucencyVolume::GetDebugMode();
		MegaLightsTranslucencyVolumeParameters.VolumeDebugSliceIndex = 0;
		MegaLightsTranslucencyVolumeParameters.LightSoftFading = 0;
		MegaLightsTranslucencyVolumeParameters.TranslucencyVolumeCascadeIndex = 0;
		MegaLightsTranslucencyVolumeParameters.TranslucencyVolumeInvResolution = 1.0f / GetTranslucencyLightingVolumeDim();
		MegaLightsTranslucencyVolumeParameters.IsUnifiedVolume = bUnifiedVolume ? 1 : 0;
		MegaLightsTranslucencyVolumeParameters.ResampleVolumeViewSize = VolumeViewSize;
		MegaLightsTranslucencyVolumeParameters.ResampleVolumeInvBufferSize = FVector3f(1.0f / VolumeBufferSize.X, 1.0f / VolumeBufferSize.Y, 1.0f / VolumeBufferSize.Z);;
		MegaLightsTranslucencyVolumeParameters.ResampleVolumeZParams = VolumeParameters.GridZParams;
	}

	const int32 TileTypeCount = Substrate::IsSubstrateEnabled() ? (int32)MegaLights::ETileType::MAX_SUBSTRATE : (int32)MegaLights::ETileType::MAX_LEGACY;
	TileAllocator = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), TileTypeCount), TEXT("MegaLights.TileAllocator"));
	TileData = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), TileDataStride * TileTypeCount), TEXT("MegaLights.TileData"));
	AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(TileAllocator), 0);

	DownsampledTileAllocator = TileAllocator;
	DownsampledTileData = TileData;

	if (DownsampleFactor.X != 1)
	{
		DownsampledTileAllocator = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), TileTypeCount), TEXT("MegaLights.DownsampledTileAllocator"));
		DownsampledTileData = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), DownsampledTileDataStride * TileTypeCount), TEXT("MegaLights.DownsampledTileData"));
		AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(DownsampledTileAllocator), 0);
	}

	// Run tile classification to generate tiles for the subsequent passes
	{
		FRDGTextureRef MegaLightsTileBitmask = nullptr;

		if (InputType == EMegaLightsInput::HairStrands)
		{
			// Create SceneDepth/SceneWorldNormal for populating history data
			FRDGTextureDesc HairDepthDesc = DownsampledSceneDepth->Desc;
			FRDGTextureDesc HairNormalDesc = DownsampledSceneWorldNormal->Desc;
			HairDepthDesc.Extent = SceneTextures.Config.Extent;
			HairNormalDesc.Extent = SceneTextures.Config.Extent;
			SceneDepth = GraphBuilder.CreateTexture(HairDepthDesc, TEXT("MegaLights.SceneDepth(HairStrands)"));
			SceneWorldNormal = GraphBuilder.CreateTexture(HairNormalDesc, TEXT("MegaLights.SceneNormal(HairStrands)"));

			HistoryScreenParameters.EncodedHistoryScreenCoordTexture = GraphBuilder.CreateTexture(
				FRDGTextureDesc::Create2D(View.GetSceneTexturesConfig().Extent, PF_R32_UINT, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV),
				TEXT("MegaLights.EncodedHistoryScreenCoord(HairStrands)"));
			HistoryScreenParameters.PackedPixelDataTexture = GraphBuilder.CreateTexture(
				FRDGTextureDesc::Create2D(View.GetSceneTexturesConfig().Extent, PF_R8_UINT, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV),
				TEXT("MegaLights.PackedPixelData(HairStrands)"));

			// MegaLights downsamples depth/normal in GenerateLightSamplesCS as it is faster but the mark shader still needs something to write to
			FRDGTextureRef PrevDownsampledSceneDepth = DownsampledSceneDepth;
			FRDGTextureRef PrevDownsampledWorldNormal = DownsampledSceneWorldNormal;
			FRDGTextureDesc DummyDownsampledDepthDesc = DownsampledSceneDepth->Desc;
			FRDGTextureDesc DummyDownsampledNormalDesc = DownsampledSceneWorldNormal->Desc;
			DummyDownsampledDepthDesc.Extent = FIntPoint(1, 1);
			DummyDownsampledNormalDesc.Extent = FIntPoint(1, 1);
			DownsampledSceneDepth = GraphBuilder.CreateTexture(DummyDownsampledDepthDesc, TEXT("MegaLights.DummyDownsampledSceneDepth"));
			DownsampledSceneWorldNormal = GraphBuilder.CreateTexture(DummyDownsampledNormalDesc, TEXT("MegaLights.DummyDownsampledWorldNormal"));

			MegaLightsTileBitmask = TileClassificationMark(0 /*ShadingPassIndex*/);

			DownsampledSceneDepth = PrevDownsampledSceneDepth;
			DownsampledSceneWorldNormal = PrevDownsampledWorldNormal;
		}
		else
		{
			// Opaque was already tile classified
			MegaLightsTileBitmask = LumenFrameTemporaries.MegaLightsTileBitmask.GetRenderTarget();
			check(LumenFrameTemporaries.HistoryScreenParameters[ViewIndex]);
			HistoryScreenParameters = *LumenFrameTemporaries.HistoryScreenParameters[ViewIndex];
			HistoryScreenParameters.EncodedHistoryScreenCoordTexture = LumenFrameTemporaries.EncodedHistoryScreenCoord.GetRenderTarget();
			HistoryScreenParameters.PackedPixelDataTexture = LumenFrameTemporaries.MegaLightsPackedPixelData.GetRenderTarget();
		}

		{
			FMegaLightsTileClassificationBuildListsCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FMegaLightsTileClassificationBuildListsCS::FParameters>();
			PassParameters->MegaLightsParameters = MegaLightsParameters;
			PassParameters->MegaLightsParameters.TileDataStride = TileDataStride;
			PassParameters->RWTileAllocator = GraphBuilder.CreateUAV(TileAllocator);
			PassParameters->RWTileData = GraphBuilder.CreateUAV(TileData);
			PassParameters->MegaLightsTileBitmask = MegaLightsTileBitmask;
			PassParameters->ViewSizeInTiles = ViewSizeInTiles;
			PassParameters->ViewMinInTiles = FMath::DivideAndRoundUp<FIntPoint>(View.ViewRect.Min, MegaLights::TileSize);
			PassParameters->DownsampledViewSizeInTiles = DownsampledViewSizeInTiles;
			PassParameters->DownsampledViewMinInTiles = FMath::DivideAndRoundUp<FIntPoint>(MegaLightsParameters.DownsampledViewMin, MegaLights::TileSize);
			PassParameters->OutputTileDataStride = TileDataStride;

			FMegaLightsTileClassificationBuildListsCS::FPermutationDomain PermutationVector;
			PermutationVector.Set<FMegaLightsTileClassificationBuildListsCS::FDownsampleFactorX>(1);
			PermutationVector.Set<FMegaLightsTileClassificationBuildListsCS::FDownsampleFactorY>(1);
			PermutationVector = FMegaLightsTileClassificationBuildListsCS::RemapPermutation(PermutationVector);
			auto ComputeShader = View.ShaderMap->GetShader<FMegaLightsTileClassificationBuildListsCS>(PermutationVector);
				 
			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("TileClassificationBuildLists"),
				ComputeShader,
				PassParameters,
				FComputeShaderUtils::GetGroupCount(ViewSizeInTiles, FMegaLightsTileClassificationBuildListsCS::GetGroupSize()));
		}

		if (DownsampleFactor.X != 1)
		{
			FMegaLightsTileClassificationBuildListsCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FMegaLightsTileClassificationBuildListsCS::FParameters>();
			PassParameters->MegaLightsParameters = MegaLightsParameters;
			PassParameters->RWTileAllocator = GraphBuilder.CreateUAV(DownsampledTileAllocator);
			PassParameters->RWTileData = GraphBuilder.CreateUAV(DownsampledTileData);
			PassParameters->MegaLightsTileBitmask = MegaLightsTileBitmask;
			PassParameters->ViewSizeInTiles = ViewSizeInTiles;
			PassParameters->ViewMinInTiles = FMath::DivideAndRoundUp<FIntPoint>(View.ViewRect.Min, MegaLights::TileSize);
			PassParameters->DownsampledViewSizeInTiles = DownsampledViewSizeInTiles;
			PassParameters->DownsampledViewMinInTiles = FMath::DivideAndRoundUp<FIntPoint>(MegaLightsParameters.DownsampledViewMin, MegaLights::TileSize);
			PassParameters->OutputTileDataStride = DownsampledTileDataStride;

			FMegaLightsTileClassificationBuildListsCS::FPermutationDomain PermutationVector;
			PermutationVector.Set<FMegaLightsTileClassificationBuildListsCS::FDownsampleFactorX>(DownsampleFactor.X);
			PermutationVector.Set<FMegaLightsTileClassificationBuildListsCS::FDownsampleFactorY>(DownsampleFactor.Y);
			PermutationVector = FMegaLightsTileClassificationBuildListsCS::RemapPermutation(PermutationVector);
			auto ComputeShader = View.ShaderMap->GetShader<FMegaLightsTileClassificationBuildListsCS>(PermutationVector);

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("DownsampledTileClassificationBuildLists"),
				ComputeShader,
				PassParameters,
				FComputeShaderUtils::GetGroupCount(DownsampledViewSizeInTiles, FMegaLightsTileClassificationBuildListsCS::GetGroupSize()));
		}
	}

	TileIndirectArgs = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDispatchIndirectParameters>(TileTypeCount), TEXT("MegaLights.TileIndirectArgs"));
	DownsampledTileIndirectArgs = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDispatchIndirectParameters>(TileTypeCount), TEXT("MegaLights.DownsampledTileIndirectArgs"));

	// Setup indirect args for classified tiles
	{
		FInitTileIndirectArgsCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FInitTileIndirectArgsCS::FParameters>();
		PassParameters->MegaLightsParameters = MegaLightsParameters;
		PassParameters->RWTileIndirectArgs = GraphBuilder.CreateUAV(TileIndirectArgs);
		PassParameters->RWDownsampledTileIndirectArgs = GraphBuilder.CreateUAV(DownsampledTileIndirectArgs);
		PassParameters->TileAllocator = GraphBuilder.CreateSRV(TileAllocator);
		PassParameters->DownsampledTileAllocator = GraphBuilder.CreateSRV(DownsampledTileAllocator);

		auto ComputeShader = View.ShaderMap->GetShader<FInitTileIndirectArgsCS>();

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("InitTileIndirectArgs"),
			ComputeShader,
			PassParameters,
			FIntVector(1, 1, 1));
	}

	// Build available tile types
	ShadingTileTypes = MegaLights::GetShadingTileTypes(InputType);

	ReferenceShadingPassCount = MegaLights::GetReferenceShadingPassCount(View.GetShaderPlatform());
	bReferenceMode = ReferenceShadingPassCount > 1;
	FirstPassStateFrameIndex = MegaLightsParameters.MegaLightsStateFrameIndex;
	AccumulatedRGBLightingDataFormat = bReferenceMode ? PF_A32B32G32R32F : PF_FloatRGB;
	AccumulatedRGBALightingDataFormat = bReferenceMode ? PF_A32B32G32R32F : PF_FloatRGBA;
	AccumulatedConfidenceDataFormat = bReferenceMode ? PF_R32_FLOAT : PF_R8;

	for (int32 i = 0; i < TVC_MAX; ++i)
	{
		TranslucencyVolumeResolvedLightingAmbient[i] = nullptr;
		TranslucencyVolumeResolvedLightingDirectional[i] = nullptr;
		TranslucencyVolumeVisibleLightHash[i] = nullptr;
	}

	// Warn about this combination as it is not fully supported
	if (bUseVSM && bReferenceMode)
	{
		UE_LOG(LogRenderer, Warning, TEXT("MegaLights Reference Mode is enabled, but VSM MegaLights are present in the scene. This setup is not fully supported and may produce artifacts!"));
	}
}

void FMegaLightsViewContext::MarkVSMPages(
	const FVirtualShadowMapArray& VirtualShadowMapArray)
{
	if (bUseVSM && MegaLights::IsMarkingVSMPages())
	{
		// TODO: VSM marking for hair strands
		if (InputType == EMegaLightsInput::HairStrands)
		{
			UE_LOG(LogRenderer, Warning, TEXT("MegaLights VSM marking is not yet implemented for HairStrands. Disable with r.MegaLights.VSM.MarkPages."));
		}
		else
		{
			MegaLights::MarkVSMPages(
				View, ViewIndex,
				GraphBuilder,
				VirtualShadowMapArray,
				SampleBufferSize,
				LightSamples,
				LightSampleRays,
				MegaLightsParameters,
				InputType);
		}
	}
}

void FMegaLightsViewContext::RayTrace(
	const FVirtualShadowMapArray& VirtualShadowMapArray,
	const TArrayView<FRDGTextureRef> NaniteShadingMasks,
	uint32 ShadingPassIndex)
{
	const bool bDebugPass = bDebug && MegaLights::IsDebugEnabledForShadingPass(ShadingPassIndex, View.GetShaderPlatform());

	MegaLights::RayTraceLightSamples(
		ViewFamily,
		View, ViewIndex,
		GraphBuilder,
		SceneTextures,
		bUseVSM ? &VirtualShadowMapArray : nullptr,
		NaniteShadingMasks,
		SampleBufferSize,
		LightSamples,
		LightSampleRays,
		VolumeSampleBufferSize,
		VolumeLightSamples,
		VolumeLightSampleRays,
		TranslucencyVolumeSampleBufferSize,
		TranslucencyVolumeLightSamples,
		TranslucencyVolumeLightSampleRays,
		MegaLightsParameters,
		MegaLightsVolumeParameters,
		MegaLightsTranslucencyVolumeParameters,
		InputType,
		bDebugPass
	);
}

struct FMegaLightsFrameTemporaries
{
	TArray<FMegaLightsViewContext, SceneRenderingAllocator> ViewContexts;
	TArray<FMegaLightsViewContext, SceneRenderingAllocator> ViewContextsHairStrands;
};

TSharedPtr<FMegaLightsFrameTemporaries> FDeferredShadingSceneRenderer::GenerateMegaLightsSamples(
	FRDGBuilder& GraphBuilder,
	const FSceneTextures& SceneTextures,
	const FLumenSceneFrameTemporaries& LumenFrameTemporaries,
	FRDGTextureRef LightingChannelsTexture)
{
	if (!MegaLights::IsEnabled(ViewFamily) || !ViewFamily.EngineShowFlags.DirectLighting)
	{
		return nullptr;
	}

	check(AreLightsInLightGrid());
	RDG_EVENT_SCOPE_STAT(GraphBuilder, MegaLights, "MegaLights");
	RDG_GPU_STAT_SCOPE(GraphBuilder, MegaLights);

	FShadowSceneRenderer& ShadowSceneRenderer = GetSceneExtensionsRenderers().GetRenderer<FShadowSceneRenderer>();
	const bool bUseVSM = ShadowSceneRenderer.AreAnyLightsUsingMegaLightsVSM();

	FBlueNoise BlueNoise = GetBlueNoiseGlobalParameters();
	TUniformBufferRef<FBlueNoise> BlueNoiseUniformBuffer = CreateUniformBufferImmediate(BlueNoise, EUniformBufferUsage::UniformBuffer_SingleDraw);

	TSharedPtr<FMegaLightsFrameTemporaries> MegaLightsFrameTemporaries(new FMegaLightsFrameTemporaries);
	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
	{
		FViewInfo& View = Views[ViewIndex];
		const bool bHairStrands = HairStrands::HasViewHairStrandsData(View) && CVarMegaLightsEnableHairStrands.GetValueOnRenderThread() > 0;

		FMegaLightsViewContext InitContext(
				GraphBuilder, 
				ViewIndex, 
				View, 
				ViewFamily, 
				Scene, 
				SceneTextures, 
			bUseVSM);
		
		FMegaLightsViewContext& ViewContext = MegaLightsFrameTemporaries->ViewContexts.Add_GetRef(InitContext);
		FMegaLightsViewContext& ViewContextsHairStrands = MegaLightsFrameTemporaries->ViewContextsHairStrands.Add_GetRef(InitContext);

		{
			RDG_EVENT_SCOPE_CONDITIONAL(GraphBuilder, bHairStrands, "GBuffer");

			ViewContext.Setup(				 
				LightingChannelsTexture,  
				LumenFrameTemporaries,
				ShouldRenderVolumetricFog(),
				GUseTranslucencyLightingVolumes && MegaLights::UseTranslucencyVolume(),
				BlueNoiseUniformBuffer, 
				EMegaLightsInput::GBuffer);

			ViewContext.GenerateSamples(				 
				LightingChannelsTexture,
				0 /* ShadingPassIndex */);

			// Mark VSM pages for any required samples
			ViewContext.MarkVSMPages(VirtualShadowMapArray);
		}

		if (bHairStrands)
		{
			RDG_EVENT_SCOPE_CONDITIONAL(GraphBuilder, bHairStrands, "HairStrands");

			ViewContextsHairStrands.Setup(
				LightingChannelsTexture, 
				LumenFrameTemporaries,
				false /*bShouldRenderVolumetricFog*/,
				false /*bShouldRenderTranslucencyVolume*/,
				BlueNoiseUniformBuffer, 
				EMegaLightsInput::HairStrands);

			ViewContextsHairStrands.GenerateSamples(
				LightingChannelsTexture,
				0 /* ShadingPassIndex */);

			ViewContextsHairStrands.MarkVSMPages(VirtualShadowMapArray);
		}
	}

	return MegaLightsFrameTemporaries;
}

static void RenderMegaLightsViewContext(
	FRDGBuilder& GraphBuilder,
	FMegaLightsViewContext& ViewContext,
	const FVirtualShadowMapArray& VirtualShadowMapArray,
	const TArrayView<FRDGTextureRef> NaniteShadingMasks,
	FRDGTextureRef LightingChannelsTexture,
	FMegaLightsVolume* MegaLightsVolume,
	FRDGTextureRef OutputColorTarget)
{
	check(ViewContext.AreSamplesGenerated());

	// In reference mode we loop over the raytracing and sample generation
	// NOTE: This does not work properly with MegaLights VSM marking as we would need
	// to go back and mark any new samples, then potentially render new shadow maps for
	// those samples as well, but this mode is designed to be used with high quality raytracing.
	for (uint32 ShadingPassIndex = 0; ShadingPassIndex < ViewContext.GetReferenceShadingPassCount(); ++ShadingPassIndex)
	{
		// We've already generated sample 0 separately, but following passes need new samples
		if (ShadingPassIndex > 0)
		{
			ViewContext.TileClassificationMark(ShadingPassIndex);

			ViewContext.GenerateSamples(LightingChannelsTexture, ShadingPassIndex);
		}
						
		ViewContext.RayTrace(
			VirtualShadowMapArray,
			NaniteShadingMasks,
			ShadingPassIndex);

		ViewContext.Resolve(
			OutputColorTarget,
			MegaLightsVolume,
			ShadingPassIndex);
	}

	ViewContext.DenoiseLighting(OutputColorTarget);
}

void FDeferredShadingSceneRenderer::RenderMegaLights(
	FRDGBuilder& GraphBuilder,
	TSharedPtr<FMegaLightsFrameTemporaries> MegaLightsFrameTemporaries,
	const FSceneTextures& SceneTextures,
	const TArrayView<FRDGTextureRef> NaniteShadingMasks,
	FRDGTextureRef LightingChannelsTexture)
{
	if (!MegaLightsFrameTemporaries.IsValid())
	{
		return;
	}

	RDG_EVENT_SCOPE_STAT(GraphBuilder, MegaLights, "MegaLights");
	RDG_GPU_STAT_SCOPE(GraphBuilder, MegaLights);

	for (int32 ViewIndex = 0; ViewIndex < MegaLightsFrameTemporaries->ViewContexts.Num(); ++ViewIndex)
	{
		FViewInfo& View = Views[ViewIndex];
		FMegaLightsViewContext& ViewContext = MegaLightsFrameTemporaries->ViewContexts[ViewIndex];
		FMegaLightsViewContext& ViewContextHairStrands = MegaLightsFrameTemporaries->ViewContextsHairStrands[ViewIndex];
		const bool bHairStrands = ViewContextHairStrands.AreSamplesGenerated();
		
		{
			RDG_EVENT_SCOPE_CONDITIONAL(GraphBuilder, bHairStrands, "GBuffer");

			RenderMegaLightsViewContext(
				GraphBuilder,
				ViewContext,
				VirtualShadowMapArray,
				NaniteShadingMasks,
				LightingChannelsTexture,
				&View.GetOwnMegaLightsVolume(),
				SceneTextures.Color.Target);
		}

		if (bHairStrands)
		{
			RDG_EVENT_SCOPE_CONDITIONAL(GraphBuilder, bHairStrands, "HairStrands");

			RenderMegaLightsViewContext(
				GraphBuilder, 
				ViewContextHairStrands,
				VirtualShadowMapArray,
				NaniteShadingMasks,
				LightingChannelsTexture, 
				nullptr /*MegaLightsVolume*/, 
				View.HairStrandsViewData.VisibilityData.SampleLightingTexture);
		}
	}
}

namespace MegaLights
{
	bool IsMissingDirectionalLightData(const FSceneViewFamily& ViewFamily)
	{
		static auto LightBufferModeCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Forward.LightBuffer.Mode"));
		
		return CVarMegaLightsDirectionalLights.GetValueOnRenderThread() && LightBufferModeCVar->GetInt() == 0;
	}

	bool HasWarning(const FSceneViewFamily& ViewFamily)
	{
		return IsRequested(ViewFamily) && (!HasRequiredTracingData(ViewFamily) || IsMissingDirectionalLightData(ViewFamily));
	}

	void WriteWarnings(const FSceneViewFamily& ViewFamily, FScreenMessageWriter& Writer)
	{
		if (!HasWarning(ViewFamily))
		{
			return;
		}

		if (!HasRequiredTracingData(ViewFamily))
		{
			static const FText MainMessage = NSLOCTEXT("Renderer", "MegaLightsCantDisplay", "MegaLights is enabled, but has no ray tracing data and won't operate correctly.");
			Writer.DrawLine(MainMessage);

#if RHI_RAYTRACING
			if (!IsRayTracingAllowed())
			{
				static const FText Message = NSLOCTEXT("Renderer", "MegaLightsCantDisplayDueToHWRTNotAllowed", "- Hardware Ray Tracing is not allowed. Check log for more info.");
				Writer.DrawLine(Message);
			}
			else if (!IsRayTracingEnabled())
			{
				static const FText Message = NSLOCTEXT("Renderer", "MegaLightsCantDisplayDueToHWRTDisabled", "- Enable 'r.RayTracing.Enable'.");
				Writer.DrawLine(Message);
			}

			static auto CVarMegaLightsHardwareRayTracing = IConsoleManager::Get().FindConsoleVariable(TEXT("r.MegaLights.HardwareRayTracing"));
			if (CVarMegaLightsHardwareRayTracing->GetInt() == 0)
			{
				static const FText Message = NSLOCTEXT("Renderer", "MegaLightsCantDisplayDueToCvar", "- Enable 'r.MegaLights.HardwareRayTracing'.");
				Writer.DrawLine(Message);
			}

			static auto CVarMegaLightsHardwareRayTracingInline = IConsoleManager::Get().FindConsoleVariable(TEXT("r.MegaLights.HardwareRayTracing.Inline"));
			if (!(GRHISupportsRayTracingShaders || (GRHISupportsInlineRayTracing && CVarMegaLightsHardwareRayTracingInline->GetInt() != 0)))
			{
				static const FText Message = NSLOCTEXT("Renderer", "MegaLightsCantDisplayDueToPlatformSettings", "- Enable Full Ray Tracing in platform platform settings or r.MegaLights.HardwareRayTracing.Inline.");
				Writer.DrawLine(Message);
			}

			if (!ViewFamily.Views[0]->IsRayTracingAllowedForView())
			{
				static const FText Message = NSLOCTEXT("Renderer", "MegaLightsCantDisplayDueToView", "- Ray Tracing not allowed on the View.");
				Writer.DrawLine(Message);
			}
#else
			static const FText Message = NSLOCTEXT("Renderer", "MegaLightsCantDisplayDueToBuild", "- Unreal Engine was built without Hardware Ray Tracing support.");
			Writer.DrawLine(Message);
#endif
		}

		if (IsMissingDirectionalLightData(ViewFamily))
		{
			static const FText MainMessage = NSLOCTEXT("Renderer", "MegaLightsCantDisplayDirectionalLights", "MegaLights requires r.Forward.LightBuffer.Mode > 0 when using r.MegaLights.DirectionalLights=1.");
			Writer.DrawLine(MainMessage);
		}
	}
}
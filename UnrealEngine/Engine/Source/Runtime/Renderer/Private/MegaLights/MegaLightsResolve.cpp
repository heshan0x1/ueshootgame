// Copyright Epic Games, Inc. All Rights Reserved.

#include "MegaLights.h"
#include "MegaLightsDefinitions.h"
#include "MegaLightsInternal.h"
#include "RendererPrivate.h"
#include "BasePassRendering.h"
#include "HairStrandsInterface.h"

static TAutoConsoleVariable<int32> CVarMegaLightsShadingConfidence(
	TEXT("r.MegaLights.ShadingConfidence"),
	1,
	TEXT("Whether to use shading confidence to reduce denoising and passthrough original signal to TSR for pixels which are well sampled."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarMegaLightsGuideByHistoryFilter(
	TEXT("r.MegaLights.GuideByHistory.Filter"),
	0,
	TEXT("Whether to filter history by sharing visibility between nearby tiles."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarMegaLightsVolumeGuideByHistoryFilter(
	TEXT("r.MegaLights.Volume.GuideByHistory.Filter"),
	1,
	TEXT("Whether to filter history by sharing visibility between nearby voxels."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarMegaLightsTranslucencyVolumeGuideByHistoryFilter(
	TEXT("r.MegaLights.TranslucencyVolume.GuideByHistory.Filter"),
	1,
	TEXT("Whether to filter history by sharing visibility between nearby voxels."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarMegaLightsScreenTraceForTransmission(
	TEXT("r.MegaLights.HairStrands.Transmittance.ScreenTrace"),
	1,
	TEXT("Use screen trace for adding fine occlusion to hair transmission."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarMegaLightsScreenTraceLengthForTransmission(
	TEXT("r.MegaLights.HairStrands.Transmittance.ScreenTraceLength"),
	10.f,
	TEXT("Screen trace length for hair transmission."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<float> CVarMegaLightsLightIndexScalarizationThreshold(
	TEXT("r.MegaLights.LightIndexScalarizationThreshold"),
	1.0f,
	TEXT("Scalarize light indices during ShadeLightSamples when wave unique light count doesn't exceed (threshold * NumSamplesPerPixel).\n")
	TEXT("0 disables scalarization while any value >= wave size forces scalarization."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

class FShadeLightSamplesCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FShadeLightSamplesCS)
	SHADER_USE_PARAMETER_STRUCT(FShadeLightSamplesCS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		RDG_BUFFER_ACCESS(IndirectArgs, ERHIAccess::IndirectArgs)
		SHADER_PARAMETER_STRUCT_INCLUDE(FMegaLightsParameters, MegaLightsParameters)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float3>, RWResolvedDiffuseLighting)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float3>, RWResolvedSpecularLighting)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, RWShadingConfidence)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float3>, RWOutputColor)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, TileAllocator)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, TileData)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, LightSamples)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, HairTransmittanceMaskTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, PackedPixelDataTexture)
		SHADER_PARAMETER(uint32, UseShadingConfidence)
		SHADER_PARAMETER(uint32, ShadingSampleIndex)
		SHADER_PARAMETER(uint32, bSubPixelShading)
		SHADER_PARAMETER(uint32, ShadingPassIndex)
	END_SHADER_PARAMETER_STRUCT()

	static int32 GetGroupSize()
	{
		return 8;
	}

	class FTileType : SHADER_PERMUTATION_INT("TILE_TYPE", (int32)MegaLights::ETileType::SHADING_MAX_SUBSTRATE);
	class FDownsampleFactorX : SHADER_PERMUTATION_RANGE_INT("DOWNSAMPLE_FACTOR_X", 1, 2);
	class FDownsampleFactorY : SHADER_PERMUTATION_RANGE_INT("DOWNSAMPLE_FACTOR_Y", 1, 2);
	class FNumSamplesPerPixel1d : SHADER_PERMUTATION_SPARSE_INT("NUM_SAMPLES_PER_PIXEL_1D", 2, 4, 16);
	class FInputType : SHADER_PERMUTATION_INT("INPUT_TYPE", int32(EMegaLightsInput::Count));
	class FDebugMode : SHADER_PERMUTATION_BOOL("DEBUG_MODE");
	class FReferenceMode : SHADER_PERMUTATION_BOOL("REFERENCE_MODE");
	class FHairComplexTransmittance: SHADER_PERMUTATION_BOOL("USE_HAIR_COMPLEX_TRANSMITTANCE");
	using FPermutationDomain = TShaderPermutationDomain<FTileType, FDownsampleFactorX, FDownsampleFactorY, FNumSamplesPerPixel1d, FInputType, FDebugMode, FReferenceMode, FHairComplexTransmittance>;

	static FPermutationDomain RemapPermutation(FPermutationDomain PermutationVector)
	{
		if (PermutationVector.Get<FDownsampleFactorY>() == 2)
		{
			PermutationVector.Set<FDownsampleFactorX>(2);
		}

		if (PermutationVector.Get<FReferenceMode>())
		{
			PermutationVector.Set<FDownsampleFactorX>(1);
			PermutationVector.Set<FDownsampleFactorY>(1);
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

		EMegaLightsInput InputType = EMegaLightsInput(PermutationVector.Get<FInputType>());
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

		OutEnvironment.CompilerFlags.Add(CFLAG_Wave32);
	}
};

IMPLEMENT_GLOBAL_SHADER(FShadeLightSamplesCS, "/Engine/Private/MegaLights/MegaLightsShading.usf", "ShadeLightSamplesCS", SF_Compute);

class FVisibleLightHashCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FVisibleLightHashCS)
	SHADER_USE_PARAMETER_STRUCT(FVisibleLightHashCS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FMegaLightsParameters, MegaLightsParameters)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, RWVisibleLightHash)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, RWVisibleLightMaskHash)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, LightSamples)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, LightSampleRays)
	END_SHADER_PARAMETER_STRUCT()

	static int32 GetGroupSize()
	{
		return 8;
	}

	class FNumSamplesPerPixel1d : SHADER_PERMUTATION_SPARSE_INT("NUM_SAMPLES_PER_PIXEL_1D", 2, 4, 16);
	class FDebugMode : SHADER_PERMUTATION_BOOL("DEBUG_MODE");
	using FPermutationDomain = TShaderPermutationDomain<FNumSamplesPerPixel1d, FDebugMode>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return MegaLights::ShouldCompileShaders(Parameters.Platform);
	}

	static EShaderPermutationPrecacheRequest ShouldPrecachePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		FPermutationDomain PermutationVector(Parameters.PermutationId);

		if (PermutationVector.Get<FDebugMode>())
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

		OutEnvironment.CompilerFlags.Add(CFLAG_WaveOperations);
		OutEnvironment.CompilerFlags.Add(CFLAG_HLSL2021);
	}
};

IMPLEMENT_GLOBAL_SHADER(FVisibleLightHashCS, "/Engine/Private/MegaLights/MegaLightsVisibleLightHash.usf", "VisibleLightHashCS", SF_Compute);

class FVolumeShadeLightSamplesCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FVolumeShadeLightSamplesCS)
	SHADER_USE_PARAMETER_STRUCT(FVolumeShadeLightSamplesCS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float3>, RWVolumeResolvedLighting)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float3>, RWTranslucencyVolumeResolvedLightingAmbient)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float3>, RWTranslucencyVolumeResolvedLightingDirectional)
		SHADER_PARAMETER_STRUCT_INCLUDE(FMegaLightsParameters, MegaLightsParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(FMegaLightsVolumeParameters, MegaLightsVolumeParameters)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D<uint>, VolumeLightSamples)
		SHADER_PARAMETER(uint32, ShadingPassIndex)
	END_SHADER_PARAMETER_STRUCT()

	static int32 GetGroupSize()
	{
		return 4;
	}

	class FTranslucencyLightingVolume : SHADER_PERMUTATION_BOOL("TRANSLUCENCY_LIGHTING_VOLUME");
	class FResampleVolume : SHADER_PERMUTATION_BOOL("RESAMPLE_VOLUME");
	class FDownsampleFactor : SHADER_PERMUTATION_RANGE_INT("VOLUME_DOWNSAMPLE_FACTOR", 1, 2);
	class FNumSamplesPerVoxel1d : SHADER_PERMUTATION_SPARSE_INT("NUM_SAMPLES_PER_VOXEL_1D", 2, 4);
	class FLightSoftFading : SHADER_PERMUTATION_BOOL("USE_LIGHT_SOFT_FADING");
	class FUseLightFunctionAtlas : SHADER_PERMUTATION_BOOL("USE_LIGHT_FUNCTION_ATLAS");
	class FDebugMode : SHADER_PERMUTATION_BOOL("DEBUG_MODE");
	class FReferenceMode : SHADER_PERMUTATION_BOOL("REFERENCE_MODE");
	using FPermutationDomain = TShaderPermutationDomain<FTranslucencyLightingVolume, FResampleVolume, FDownsampleFactor, FNumSamplesPerVoxel1d, FLightSoftFading, FUseLightFunctionAtlas, FDebugMode, FReferenceMode>;

	static FPermutationDomain RemapPermutation(FPermutationDomain PermutationVector)
	{
		if (PermutationVector.Get<FReferenceMode>())
		{
			PermutationVector.Set<FDownsampleFactor>(1);
		}

		return PermutationVector;
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		const FPermutationDomain PermutationVector(Parameters.PermutationId);

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

		FPermutationDomain PermutationVector(Parameters.PermutationId);
		const int32 NumSamplesPerVoxel1d = PermutationVector.Get<FNumSamplesPerVoxel1d>();
		const FIntVector NumSamplesPerVoxel3d = MegaLights::GetNumSamplesPerVoxel3d(NumSamplesPerVoxel1d);
		OutEnvironment.SetDefine(TEXT("NUM_SAMPLES_PER_VOXEL_3D_X"), NumSamplesPerVoxel3d.X);
		OutEnvironment.SetDefine(TEXT("NUM_SAMPLES_PER_VOXEL_3D_Y"), NumSamplesPerVoxel3d.Y);
		OutEnvironment.SetDefine(TEXT("NUM_SAMPLES_PER_VOXEL_3D_Z"), NumSamplesPerVoxel3d.Z);

		OutEnvironment.CompilerFlags.Add(CFLAG_Wave32);
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

IMPLEMENT_GLOBAL_SHADER(FVolumeShadeLightSamplesCS, "/Engine/Private/MegaLights/MegaLightsVolumeShading.usf", "VolumeShadeLightSamplesCS", SF_Compute);

class FVolumeVisibleLightHashCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FVolumeVisibleLightHashCS)
	SHADER_USE_PARAMETER_STRUCT(FVolumeVisibleLightHashCS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FMegaLightsParameters, MegaLightsParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(FMegaLightsVolumeParameters, MegaLightsVolumeParameters)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, RWVisibleLightHash)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D<uint>, LightSamples)
		SHADER_PARAMETER(FIntVector, VolumeVisibleLightHashTileSize)
		SHADER_PARAMETER(FIntVector, VolumeVisibleLightHashViewSizeInTiles)
	END_SHADER_PARAMETER_STRUCT()

	static int32 GetGroupSize()
	{
		return 4;
	}

	class FNumSamplesPerVoxel1d : SHADER_PERMUTATION_SPARSE_INT("NUM_SAMPLES_PER_VOXEL_1D", 2, 4);
	class FDebugMode : SHADER_PERMUTATION_BOOL("DEBUG_MODE");
	using FPermutationDomain = TShaderPermutationDomain<FNumSamplesPerVoxel1d, FDebugMode>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return MegaLights::ShouldCompileShaders(Parameters.Platform);
	}

	static EShaderPermutationPrecacheRequest ShouldPrecachePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		FPermutationDomain PermutationVector(Parameters.PermutationId);

		if (PermutationVector.Get<FDebugMode>())
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
		const int32 NumSamplesPerPixel1d = PermutationVector.Get<FNumSamplesPerVoxel1d>();
		const int32 NumSamplesPerVoxel1d = PermutationVector.Get<FNumSamplesPerVoxel1d>();
		const FIntVector NumSamplesPerVoxel3d = MegaLights::GetNumSamplesPerVoxel3d(NumSamplesPerVoxel1d);
		OutEnvironment.SetDefine(TEXT("NUM_SAMPLES_PER_VOXEL_3D_X"), NumSamplesPerVoxel3d.X);
		OutEnvironment.SetDefine(TEXT("NUM_SAMPLES_PER_VOXEL_3D_Y"), NumSamplesPerVoxel3d.Y);
		OutEnvironment.SetDefine(TEXT("NUM_SAMPLES_PER_VOXEL_3D_Z"), NumSamplesPerVoxel3d.Z);

		OutEnvironment.CompilerFlags.Add(CFLAG_WaveOperations);
		OutEnvironment.CompilerFlags.Add(CFLAG_HLSL2021);
	}
};

IMPLEMENT_GLOBAL_SHADER(FVolumeVisibleLightHashCS, "/Engine/Private/MegaLights/MegaLightsVisibleLightHash.usf", "VolumeVisibleLightHashCS", SF_Compute);

class FVolumeFilterVisibleLightHashCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FVolumeFilterVisibleLightHashCS)
	SHADER_USE_PARAMETER_STRUCT(FVolumeFilterVisibleLightHashCS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FMegaLightsParameters, MegaLightsParameters)
		SHADER_PARAMETER(FIntVector, VolumeVisibleLightHashViewSizeInTiles)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, RWVisibleLightHash)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, VisibleLightHashBuffer)
	END_SHADER_PARAMETER_STRUCT()

	static int32 GetGroupSize()
	{
		return 4;
	}

	class FDebugMode : SHADER_PERMUTATION_BOOL("DEBUG_MODE");
	using FPermutationDomain = TShaderPermutationDomain<FDebugMode>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return MegaLights::ShouldCompileShaders(Parameters.Platform);
	}

	static EShaderPermutationPrecacheRequest ShouldPrecachePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		FPermutationDomain PermutationVector(Parameters.PermutationId);

		if (PermutationVector.Get<FDebugMode>())
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

		OutEnvironment.CompilerFlags.Add(CFLAG_WaveOperations);
		OutEnvironment.CompilerFlags.Add(CFLAG_HLSL2021);
	}
};

IMPLEMENT_GLOBAL_SHADER(FVolumeFilterVisibleLightHashCS, "/Engine/Private/MegaLights/MegaLightsFilterVisibleLightHash.usf", "VolumeFilterVisibleLightHashCS", SF_Compute);

class FClearResolvedLightingCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FClearResolvedLightingCS)
	SHADER_USE_PARAMETER_STRUCT(FClearResolvedLightingCS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		RDG_BUFFER_ACCESS(IndirectArgs, ERHIAccess::IndirectArgs)
		SHADER_PARAMETER_STRUCT_INCLUDE(FMegaLightsParameters, MegaLightsParameters)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float3>, RWResolvedDiffuseLighting)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float3>, RWResolvedSpecularLighting)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, TileAllocator)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, TileData)
	END_SHADER_PARAMETER_STRUCT()

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
};

IMPLEMENT_GLOBAL_SHADER(FClearResolvedLightingCS, "/Engine/Private/MegaLights/MegaLightsShading.usf", "ClearResolvedLightingCS", SF_Compute);

class FFilterVisibleLightHashCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FFilterVisibleLightHashCS)
	SHADER_USE_PARAMETER_STRUCT(FFilterVisibleLightHashCS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FMegaLightsParameters, MegaLightsParameters)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, RWVisibleLightHash)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, RWVisibleLightMaskHash)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, VisibleLightHashBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, VisibleLightMaskHashBuffer)
	END_SHADER_PARAMETER_STRUCT()

	static int32 GetGroupSize()
	{
		return 8;
	}

	class FDebugMode : SHADER_PERMUTATION_BOOL("DEBUG_MODE");
	using FPermutationDomain = TShaderPermutationDomain<FDebugMode>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return MegaLights::ShouldCompileShaders(Parameters.Platform);
	}

	static EShaderPermutationPrecacheRequest ShouldPrecachePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		FPermutationDomain PermutationVector(Parameters.PermutationId);

		if (PermutationVector.Get<FDebugMode>())
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

		OutEnvironment.CompilerFlags.Add(CFLAG_HLSL2021);
	}
};

IMPLEMENT_GLOBAL_SHADER(FFilterVisibleLightHashCS, "/Engine/Private/MegaLights/MegaLightsFilterVisibleLightHash.usf", "FilterVisibleLightHashCS", SF_Compute);

class FMegaLightHairTransmittanceCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FMegaLightHairTransmittanceCS)
	SHADER_USE_PARAMETER_STRUCT(FMegaLightHairTransmittanceCS, FGlobalShader)

		class FNumSamplesPerPixel1d : SHADER_PERMUTATION_SPARSE_INT("NUM_SAMPLES_PER_PIXEL_1D", 2, 4, 16);
	using FPermutationDomain = TShaderPermutationDomain<FNumSamplesPerPixel1d>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FMegaLightsParameters, MegaLightsParameters)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FVirtualVoxelParameters, VirtualVoxel)
		SHADER_PARAMETER(uint32, bUseScreenTrace)
		SHADER_PARAMETER(float, ScreenTraceLength)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, LightSamples)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, LightSampleRays)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, RWTransmittanceMaskTexture)
	END_SHADER_PARAMETER_STRUCT()

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
		FForwardLightingParameters::ModifyCompilationEnvironment(Parameters.Platform, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GetGroupSize());
		OutEnvironment.SetDefine(TEXT("INPUT_TYPE"), TEXT("INPUT_TYPE_HAIRSTRANDS"));

		FPermutationDomain PermutationVector(Parameters.PermutationId);
		const int32 NumSamplesPerPixel1d = PermutationVector.Get<FNumSamplesPerPixel1d>();
		const FIntPoint NumSamplesPerPixel2d = MegaLights::GetNumSamplesPerPixel2d(NumSamplesPerPixel1d);
		OutEnvironment.SetDefine(TEXT("NUM_SAMPLES_PER_PIXEL_2D_X"), NumSamplesPerPixel2d.X);
		OutEnvironment.SetDefine(TEXT("NUM_SAMPLES_PER_PIXEL_2D_Y"), NumSamplesPerPixel2d.Y);
	}
};

IMPLEMENT_GLOBAL_SHADER(FMegaLightHairTransmittanceCS, "/Engine/Private/MegaLights/MegaLights.usf", "HairTransmittanceCS", SF_Compute);

void FMegaLightsViewContext::Resolve(
	FRDGTextureRef OutputColorTarget,
	FMegaLightsVolume* MegaLightsVolume,
	uint32 ShadingPassIndex)
{
	if (MegaLightsVolume)
	{
		MegaLightsVolume->Texture = nullptr;
	}

	const bool bDebugPass = bDebug && MegaLights::IsDebugEnabledForShadingPass(ShadingPassIndex, View.GetShaderPlatform());

	// Compute transmittance estimate for hair sample
	FRDGTextureRef HairTransmittanceMaskTexture = nullptr;
	if (InputType == EMegaLightsInput::HairStrands)
	{
		HairTransmittanceMaskTexture = GraphBuilder.CreateTexture(
			FRDGTextureDesc::Create2D(DonwnsampledSampleBufferSize, PF_R32_UINT, FClearValueBinding::None, TexCreate_ShaderResource | TexCreate_UAV),
			TEXT("MegaLights.HairTransmittance"));

		FMegaLightHairTransmittanceCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FMegaLightHairTransmittanceCS::FParameters>();
		PassParameters->MegaLightsParameters = MegaLightsParameters;
		PassParameters->VirtualVoxel = HairStrands::BindHairStrandsVoxelUniformParameters(View);
		PassParameters->LightSamples = LightSamples;
		PassParameters->LightSampleRays = LightSampleRays;
		PassParameters->RWTransmittanceMaskTexture = GraphBuilder.CreateUAV(HairTransmittanceMaskTexture);

		// Screen trace
		PassParameters->MegaLightsParameters.HZBParameters = GetHZBParameters(GraphBuilder, View, EHZBType::ClosestHZB, View.HairStrandsViewData.VisibilityData.HairOnlyDepthFurthestHZBTexture, View.HairStrandsViewData.VisibilityData.HairOnlyDepthClosestHZBTexture);
		PassParameters->bUseScreenTrace = CVarMegaLightsScreenTraceForTransmission.GetValueOnRenderThread() > 0 ? 1u : 0u;
		PassParameters->ScreenTraceLength = FMath::Max(CVarMegaLightsScreenTraceLengthForTransmission.GetValueOnRenderThread(), 0.f);

		FMegaLightHairTransmittanceCS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FMegaLightHairTransmittanceCS::FNumSamplesPerPixel1d>(NumSamplesPerPixel2d.X * NumSamplesPerPixel2d.Y);
		auto ComputeShader = View.ShaderMap->GetShader<FMegaLightHairTransmittanceCS>(PermutationVector);

		const FIntVector GroupCount = FComputeShaderUtils::GetGroupCount(DonwnsampledSampleBufferSize, FMegaLightHairTransmittanceCS::GetGroupSize());
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("HairTransmittanceCS"),
			ComputeShader,
			PassParameters,
			GroupCount);
	}

	if (ShadingPassIndex == 0)
	{
		ResolvedDiffuseLighting = GraphBuilder.CreateTexture(
			FRDGTextureDesc::Create2D(View.GetSceneTexturesConfig().Extent, AccumulatedRGBLightingDataFormat, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV),
			TEXT("MegaLights.ResolvedDiffuseLighting"));

		ResolvedSpecularLighting = GraphBuilder.CreateTexture(
			FRDGTextureDesc::Create2D(View.GetSceneTexturesConfig().Extent, AccumulatedRGBLightingDataFormat, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV),
			TEXT("MegaLights.ResolvedSpecularLighting"));

		ShadingConfidence = GraphBuilder.CreateTexture(
			FRDGTextureDesc::Create2D(View.GetSceneTexturesConfig().Extent, AccumulatedConfidenceDataFormat, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV),
			TEXT("MegaLights.ShadingConfidence"));
	}

	VisibleLightHash = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), VisibleLightHashBufferSize), TEXT("MegaLights.VisibleLightHash"));
	VisibleLightMaskHash = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), VisibleLightHashBufferSize), TEXT("MegaLights.VisibleLightMaskHash"));
	AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(VisibleLightHash), 0);
	AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(VisibleLightMaskHash), 0);

	if (bVolumeEnabled && bVolumeGuideByHistory)
	{
		VolumeVisibleLightHash = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), VolumeVisibleLightHashBufferSize), TEXT("MegaLights.Volume.VisibleLightHash"));
		AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(VolumeVisibleLightHash), 0);
	}

	if (MegaLights::UseTranslucencyVolume() && bShouldRenderTranslucencyVolume && bTranslucencyVolumeGuideByHistory && !bUnifiedVolume)
	{
		for (uint32 CascadeIndex = 0; CascadeIndex < TVC_MAX; ++CascadeIndex)
		{
			TranslucencyVolumeVisibleLightHash[CascadeIndex] = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), TranslucencyVolumeVisibleLightHashBufferSize), TEXT("MegaLights.TranslucencyVolume.VisibleLightHash"));
			AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(TranslucencyVolumeVisibleLightHash[CascadeIndex]), 0);
		}
	}

	// Shade light samples
	{
		FRDGTextureUAVRef ResolvedDiffuseLightingUAV = GraphBuilder.CreateUAV(ResolvedDiffuseLighting, ERDGUnorderedAccessViewFlags::SkipBarrier);
		FRDGTextureUAVRef ResolvedSpecularLightingUAV = GraphBuilder.CreateUAV(ResolvedSpecularLighting, ERDGUnorderedAccessViewFlags::SkipBarrier);
		FRDGTextureUAVRef ShadingConfidenceUAV = GraphBuilder.CreateUAV(ShadingConfidence, ERDGUnorderedAccessViewFlags::SkipBarrier);
		FRDGBufferUAVRef VisibleLightHashUAV = GraphBuilder.CreateUAV(VisibleLightHash, ERDGUnorderedAccessViewFlags::SkipBarrier);
		FRDGBufferUAVRef VisibleLightMaskHashUAV = GraphBuilder.CreateUAV(VisibleLightMaskHash, ERDGUnorderedAccessViewFlags::SkipBarrier);
		FRDGTextureUAVRef OutputColorTargetUAV = GraphBuilder.CreateUAV(OutputColorTarget, ERDGUnorderedAccessViewFlags::SkipBarrier);

		// Clear tiles which won't be processed by FShadeLightSamplesCS
		{
			FClearResolvedLightingCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FClearResolvedLightingCS::FParameters>();
			PassParameters->IndirectArgs = TileIndirectArgs;
			PassParameters->RWResolvedDiffuseLighting = ResolvedDiffuseLightingUAV;
			PassParameters->RWResolvedSpecularLighting = ResolvedSpecularLightingUAV;
			PassParameters->MegaLightsParameters = MegaLightsParameters;
			PassParameters->TileAllocator = GraphBuilder.CreateSRV(TileAllocator);
			PassParameters->TileData = GraphBuilder.CreateSRV(TileData);

			auto ComputeShader = View.ShaderMap->GetShader<FClearResolvedLightingCS>();

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("ClearResolvedLighting"),
				ComputeShader,
				PassParameters,
				TileIndirectArgs,
				(int32)MegaLights::ETileType::Empty * sizeof(FRHIDispatchIndirectParameters));
		}

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

			const bool bIsComplexTile = IsComplexTileType(TileType);
			const uint32 SampleCount = InputType == EMegaLightsInput::HairStrands && bSubPixelShading ? View.HairStrandsViewData.VisibilityData.MaxSampleCount : 1u;
			for (uint32 SampleIt = 0; SampleIt < SampleCount; ++SampleIt)
			{
				FShadeLightSamplesCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FShadeLightSamplesCS::FParameters>();
				PassParameters->RWResolvedDiffuseLighting = ResolvedDiffuseLightingUAV;
				PassParameters->RWResolvedSpecularLighting = ResolvedSpecularLightingUAV;
				PassParameters->RWShadingConfidence = ShadingConfidenceUAV;
				PassParameters->RWOutputColor = OutputColorTargetUAV;
				PassParameters->IndirectArgs = TileIndirectArgs;
				PassParameters->MegaLightsParameters = MegaLightsParameters;
				PassParameters->TileAllocator = GraphBuilder.CreateSRV(TileAllocator);
				PassParameters->TileData = GraphBuilder.CreateSRV(TileData);
				PassParameters->LightSamples = LightSamples;
				PassParameters->UseShadingConfidence = CVarMegaLightsShadingConfidence.GetValueOnRenderThread();
				PassParameters->HairTransmittanceMaskTexture = HairTransmittanceMaskTexture;
				PassParameters->PackedPixelDataTexture = HistoryScreenParameters.PackedPixelDataTexture;
				PassParameters->ShadingSampleIndex = SampleIt;
				PassParameters->bSubPixelShading = bSubPixelShading ? 1u : 0u;
				PassParameters->ShadingPassIndex = ShadingPassIndex;

				FShadeLightSamplesCS::FPermutationDomain PermutationVector;
				PermutationVector.Set<FShadeLightSamplesCS::FTileType>(ShadingTileType);
				PermutationVector.Set<FShadeLightSamplesCS::FDownsampleFactorX>(DownsampleFactor.X);
				PermutationVector.Set<FShadeLightSamplesCS::FDownsampleFactorY>(DownsampleFactor.Y);
				PermutationVector.Set<FShadeLightSamplesCS::FNumSamplesPerPixel1d>(NumSamplesPerPixel2d.X * NumSamplesPerPixel2d.Y);
				PermutationVector.Set<FShadeLightSamplesCS::FInputType>(uint32(InputType));
				PermutationVector.Set<FShadeLightSamplesCS::FDebugMode>(bDebugPass);
				PermutationVector.Set<FShadeLightSamplesCS::FReferenceMode>(bReferenceMode);
				PermutationVector.Set<FShadeLightSamplesCS::FHairComplexTransmittance>(bHairComplexTransmittance && bIsComplexTile);
				auto ComputeShader = View.ShaderMap->GetShader<FShadeLightSamplesCS>(PermutationVector);

				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME("ShadeLightSamples TileType:%s Sample:%d", MegaLights::GetTileTypeString(TileType), SampleIt),
					ComputeShader,
					PassParameters,
					TileIndirectArgs,
					ShadingTileType * sizeof(FRHIDispatchIndirectParameters));
			}
		}
	}

	// Prepare visible light list hash for the next frame or pass
	if (bGuideByHistory)
	{
		FVisibleLightHashCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FVisibleLightHashCS::FParameters>();
		PassParameters->RWVisibleLightHash = GraphBuilder.CreateUAV(VisibleLightHash);
		PassParameters->RWVisibleLightMaskHash = GraphBuilder.CreateUAV(VisibleLightMaskHash);
		PassParameters->MegaLightsParameters = MegaLightsParameters;
		PassParameters->LightSamples = LightSamples;
		PassParameters->LightSampleRays = LightSampleRays;

		FVisibleLightHashCS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FVisibleLightHashCS::FNumSamplesPerPixel1d>(NumSamplesPerPixel2d.X * NumSamplesPerPixel2d.Y);
		PermutationVector.Set<FVisibleLightHashCS::FDebugMode>(bDebugPass);
		auto ComputeShader = View.ShaderMap->GetShader<FVisibleLightHashCS>(PermutationVector);

		const FIntVector GroupCount = FComputeShaderUtils::GetGroupCount(View.ViewRect.Size(), FVisibleLightHashCS::GetGroupSize());

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("VisibleLightHash"),
			ComputeShader,
			PassParameters,
			GroupCount);
	}

	if (bVolumeEnabled && bVolumeGuideByHistory)
	{
		FVolumeVisibleLightHashCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FVolumeVisibleLightHashCS::FParameters>();
		PassParameters->RWVisibleLightHash = GraphBuilder.CreateUAV(VolumeVisibleLightHash);
		PassParameters->MegaLightsParameters = MegaLightsParameters;
		PassParameters->MegaLightsVolumeParameters = MegaLightsVolumeParameters;
		PassParameters->LightSamples = VolumeLightSamples;
		PassParameters->VolumeVisibleLightHashTileSize = VolumeVisibleLightHashTileSize;
		PassParameters->VolumeVisibleLightHashViewSizeInTiles = VolumeVisibleLightHashViewSizeInTiles;

		FVolumeVisibleLightHashCS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FVolumeVisibleLightHashCS::FNumSamplesPerVoxel1d>(NumSamplesPerVoxel3d.X * NumSamplesPerVoxel3d.Y * NumSamplesPerVoxel3d.Z);
		PermutationVector.Set<FVolumeVisibleLightHashCS::FDebugMode>(bDebugPass);
		auto ComputeShader = View.ShaderMap->GetShader<FVolumeVisibleLightHashCS>(PermutationVector);

		const FIntVector GroupCount = FComputeShaderUtils::GetGroupCount(VolumeVisibleLightHashViewSizeInTiles, FVolumeVisibleLightHashCS::GetGroupSize());

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("VolumeVisibleLightHash"),
			ComputeShader,
			PassParameters,
			GroupCount);
	}

	if (MegaLights::UseTranslucencyVolume() && bShouldRenderTranslucencyVolume && bTranslucencyVolumeGuideByHistory && !bUnifiedVolume)
	{
		for (uint32 CascadeIndex = 0; CascadeIndex < TVC_MAX; ++CascadeIndex)
		{
			FVolumeVisibleLightHashCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FVolumeVisibleLightHashCS::FParameters>();
			PassParameters->RWVisibleLightHash = GraphBuilder.CreateUAV(TranslucencyVolumeVisibleLightHash[CascadeIndex]);
			PassParameters->MegaLightsParameters = MegaLightsParameters;
			PassParameters->MegaLightsVolumeParameters = MegaLightsTranslucencyVolumeParameters;
			PassParameters->MegaLightsVolumeParameters.TranslucencyVolumeCascadeIndex = CascadeIndex;
			PassParameters->LightSamples = TranslucencyVolumeLightSamples[CascadeIndex];
			PassParameters->VolumeVisibleLightHashTileSize = TranslucencyVolumeVisibleLightHashTileSize;
			PassParameters->VolumeVisibleLightHashViewSizeInTiles = TranslucencyVolumeVisibleLightHashSizeInTiles;

			FVolumeVisibleLightHashCS::FPermutationDomain PermutationVector;
			PermutationVector.Set<FVolumeVisibleLightHashCS::FNumSamplesPerVoxel1d>(NumSamplesPerTranslucencyVoxel3d.X * NumSamplesPerTranslucencyVoxel3d.Y * NumSamplesPerTranslucencyVoxel3d.Z);
			PermutationVector.Set<FVolumeVisibleLightHashCS::FDebugMode>(bDebugPass);
			auto ComputeShader = View.ShaderMap->GetShader<FVolumeVisibleLightHashCS>(PermutationVector);

			const FIntVector GroupCount = FComputeShaderUtils::GetGroupCount(TranslucencyVolumeVisibleLightHashSizeInTiles, FVolumeVisibleLightHashCS::GetGroupSize());

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("TranslucencyVolumeVisibleLightHash"),
				ComputeShader,
				PassParameters,
				GroupCount);
		}
	}

	if (MegaLights::UseVolume() && bShouldRenderVolumetricFog)
	{
		if (ShadingPassIndex == 0)
		{
			VolumeResolvedLighting = GraphBuilder.CreateTexture(
				FRDGTextureDesc::Create3D(VolumetricFogParamaters.ResourceGridSizeInt, AccumulatedRGBLightingDataFormat, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV | TexCreate_3DTiling),
				TEXT("MegaLights.Volume.ResolvedLighting"));
		}

		FVolumeShadeLightSamplesCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FVolumeShadeLightSamplesCS::FParameters>();
		PassParameters->RWVolumeResolvedLighting = GraphBuilder.CreateUAV(VolumeResolvedLighting);
		PassParameters->MegaLightsParameters = MegaLightsParameters;
		PassParameters->MegaLightsVolumeParameters = MegaLightsVolumeParameters;
		PassParameters->VolumeLightSamples = VolumeLightSamples;
		PassParameters->ShadingPassIndex = ShadingPassIndex;

		// patch relevant parameters to match volumetric fog
		PassParameters->MegaLightsVolumeParameters.VolumeViewSize = VolumetricFogParamaters.ViewGridSizeInt;
		PassParameters->MegaLightsVolumeParameters.VolumeInvBufferSize = FVector3f(1.0f / VolumetricFogParamaters.ResourceGridSizeInt.X, 1.0f / VolumetricFogParamaters.ResourceGridSizeInt.Y, 1.0f / VolumetricFogParamaters.ResourceGridSizeInt.Z);
		PassParameters->MegaLightsVolumeParameters.MegaLightsVolumeZParams = VolumetricFogParamaters.GridZParams;
		PassParameters->MegaLightsVolumeParameters.MegaLightsVolumePixelSize = VolumetricFogParamaters.FogGridToPixelXY.X;

		FVolumeShadeLightSamplesCS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FVolumeShadeLightSamplesCS::FTranslucencyLightingVolume>(false);
		PermutationVector.Set<FVolumeShadeLightSamplesCS::FResampleVolume>(bUnifiedVolume);
		PermutationVector.Set<FVolumeShadeLightSamplesCS::FDownsampleFactor>(VolumeDownsampleFactor);
		PermutationVector.Set<FVolumeShadeLightSamplesCS::FNumSamplesPerVoxel1d>(NumSamplesPerVoxel3d.X * NumSamplesPerVoxel3d.Y * NumSamplesPerVoxel3d.Z);
		PermutationVector.Set<FVolumeShadeLightSamplesCS::FLightSoftFading>(PassParameters->MegaLightsVolumeParameters.LightSoftFading > 0.0f);
		PermutationVector.Set<FVolumeShadeLightSamplesCS::FUseLightFunctionAtlas>(bUseLightFunctionAtlas && MegaLightsVolume::UsesLightFunction());
		PermutationVector.Set<FVolumeShadeLightSamplesCS::FDebugMode>(bVolumeDebug);
		PermutationVector.Set<FVolumeShadeLightSamplesCS::FReferenceMode>(bReferenceMode);
		auto ComputeShader = View.ShaderMap->GetShader<FVolumeShadeLightSamplesCS>(PermutationVector);

		const FIntVector GroupCount = FComputeShaderUtils::GetGroupCount(VolumetricFogParamaters.ViewGridSizeInt, FVolumeShadeLightSamplesCS::GetGroupSize());

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("VolumeShadeLightSamples"),
			ComputeShader,
			PassParameters,
			GroupCount);

		if (MegaLightsVolume)
		{
			MegaLightsVolume->Texture = VolumeResolvedLighting;
		}
	}

	if (MegaLights::UseTranslucencyVolume() && bShouldRenderTranslucencyVolume)
	{
		for (uint32 CascadeIndex = 0; CascadeIndex < TVC_MAX; ++CascadeIndex)
		{
			if (ShadingPassIndex == 0)
			{
				TranslucencyVolumeResolvedLightingAmbient[CascadeIndex] = GraphBuilder.CreateTexture(
					FRDGTextureDesc::Create3D(TranslucencyVolumeBufferSize, AccumulatedRGBALightingDataFormat, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV | TexCreate_3DTiling),
					TEXT("MegaLights.TranslucencyVolume.ResolvedLightingAmbient"));
				TranslucencyVolumeResolvedLightingDirectional[CascadeIndex] = GraphBuilder.CreateTexture(
					FRDGTextureDesc::Create3D(TranslucencyVolumeBufferSize, AccumulatedRGBALightingDataFormat, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV | TexCreate_3DTiling),
					TEXT("MegaLights.TranslucencyVolume.ResolvedLightingDirectional"));
			}

			FVolumeShadeLightSamplesCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FVolumeShadeLightSamplesCS::FParameters>();
			PassParameters->RWTranslucencyVolumeResolvedLightingAmbient = GraphBuilder.CreateUAV(TranslucencyVolumeResolvedLightingAmbient[CascadeIndex]);
			PassParameters->RWTranslucencyVolumeResolvedLightingDirectional = GraphBuilder.CreateUAV(TranslucencyVolumeResolvedLightingDirectional[CascadeIndex]);
			PassParameters->MegaLightsParameters = MegaLightsParameters;
			PassParameters->MegaLightsVolumeParameters = MegaLightsTranslucencyVolumeParameters;
			PassParameters->MegaLightsVolumeParameters.TranslucencyVolumeCascadeIndex = CascadeIndex;
			PassParameters->VolumeLightSamples = bUnifiedVolume ? VolumeLightSamples : TranslucencyVolumeLightSamples[CascadeIndex];
			PassParameters->ShadingPassIndex = ShadingPassIndex;

			FVolumeShadeLightSamplesCS::FPermutationDomain PermutationVector;
			PermutationVector.Set<FVolumeShadeLightSamplesCS::FTranslucencyLightingVolume>(true);
			PermutationVector.Set<FVolumeShadeLightSamplesCS::FResampleVolume>(bUnifiedVolume);
			PermutationVector.Set<FVolumeShadeLightSamplesCS::FDownsampleFactor>(TranslucencyVolumeDownsampleFactor);
			PermutationVector.Set<FVolumeShadeLightSamplesCS::FNumSamplesPerVoxel1d>(NumSamplesPerTranslucencyVoxel3d.X * NumSamplesPerTranslucencyVoxel3d.Y * NumSamplesPerTranslucencyVoxel3d.Z);
			PermutationVector.Set<FVolumeShadeLightSamplesCS::FLightSoftFading>(false);
			PermutationVector.Set<FVolumeShadeLightSamplesCS::FUseLightFunctionAtlas >(bUseLightFunctionAtlas && MegaLightsTranslucencyVolume::UsesLightFunction());
			PermutationVector.Set<FVolumeShadeLightSamplesCS::FDebugMode>(bTranslucencyVolumeDebug);
			PermutationVector.Set<FVolumeShadeLightSamplesCS::FReferenceMode>(bReferenceMode);
			auto ComputeShader = View.ShaderMap->GetShader<FVolumeShadeLightSamplesCS>(PermutationVector);

			const FIntVector GroupCount = FComputeShaderUtils::GetGroupCount(TranslucencyVolumeBufferSize, FVolumeShadeLightSamplesCS::GetGroupSize());

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("TranslucencyVolumeShadeLightSamples"),
				ComputeShader,
				PassParameters,
				GroupCount);

			if (MegaLightsVolume)
			{
				MegaLightsVolume->TranslucencyAmbient[CascadeIndex] = TranslucencyVolumeResolvedLightingAmbient[CascadeIndex];
				MegaLightsVolume->TranslucencyDirectional[CascadeIndex] = TranslucencyVolumeResolvedLightingDirectional[CascadeIndex];
			}
		}
	}

	if (bGuideByHistory && CVarMegaLightsGuideByHistoryFilter.GetValueOnRenderThread())
	{
		FRDGBufferRef FilteredVisibleLightHash = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), VisibleLightHashBufferSize), TEXT("MegaLights.FilteredVisibleLightHash"));
		FRDGBufferRef FilteredVisibleLightMaskHash = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), VisibleLightHashBufferSize), TEXT("MegaLights.FilteredVisibleLightMaskHash"));

		FFilterVisibleLightHashCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FFilterVisibleLightHashCS::FParameters>();
		PassParameters->RWVisibleLightHash = GraphBuilder.CreateUAV(FilteredVisibleLightHash);
		PassParameters->RWVisibleLightMaskHash = GraphBuilder.CreateUAV(FilteredVisibleLightMaskHash);
		PassParameters->MegaLightsParameters = MegaLightsParameters;
		PassParameters->VisibleLightHashBuffer = GraphBuilder.CreateSRV(VisibleLightHash);
		PassParameters->VisibleLightMaskHashBuffer = GraphBuilder.CreateSRV(VisibleLightMaskHash);

		FFilterVisibleLightHashCS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FFilterVisibleLightHashCS::FDebugMode>(bDebugPass);
		auto ComputeShader = View.ShaderMap->GetShader<FFilterVisibleLightHashCS>(PermutationVector);

		const FIntVector GroupCount = FComputeShaderUtils::GetGroupCount(VisibleLightHashViewSizeInTiles, FFilterVisibleLightHashCS::GetGroupSize());

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("FilterVisibleLightHash"),
			ComputeShader,
			PassParameters,
			GroupCount);

		VisibleLightHash = FilteredVisibleLightHash;
		VisibleLightMaskHash = FilteredVisibleLightMaskHash;
	}

	if (bVolumeEnabled && bVolumeGuideByHistory && CVarMegaLightsVolumeGuideByHistoryFilter.GetValueOnRenderThread())
	{
		FRDGBufferRef VolumeFilteredVisibleLightHash = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), VolumeVisibleLightHashBufferSize), TEXT("MegaLights.Volume.FilteredVisibleLightHash"));

		FVolumeFilterVisibleLightHashCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FVolumeFilterVisibleLightHashCS::FParameters>();
		PassParameters->RWVisibleLightHash = GraphBuilder.CreateUAV(VolumeFilteredVisibleLightHash);
		PassParameters->MegaLightsParameters = MegaLightsParameters;
		PassParameters->VolumeVisibleLightHashViewSizeInTiles = VolumeVisibleLightHashViewSizeInTiles;
		PassParameters->VisibleLightHashBuffer = GraphBuilder.CreateSRV(VolumeVisibleLightHash);

		FVolumeFilterVisibleLightHashCS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FVolumeFilterVisibleLightHashCS::FDebugMode>(bDebugPass);
		auto ComputeShader = View.ShaderMap->GetShader<FVolumeFilterVisibleLightHashCS>(PermutationVector);

		const FIntVector GroupCount = FComputeShaderUtils::GetGroupCount(VolumeVisibleLightHashViewSizeInTiles, FVolumeFilterVisibleLightHashCS::GetGroupSize());

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("VolumeFilterVisibleLightHash"),
			ComputeShader,
			PassParameters,
			GroupCount);

		VolumeVisibleLightHash = VolumeFilteredVisibleLightHash;
	}

	if (MegaLights::UseTranslucencyVolume() && bShouldRenderTranslucencyVolume && bTranslucencyVolumeGuideByHistory && CVarMegaLightsTranslucencyVolumeGuideByHistoryFilter.GetValueOnRenderThread() && !bUnifiedVolume)
	{
		for (uint32 CascadeIndex = 0; CascadeIndex < TVC_MAX; ++CascadeIndex)
		{
			FRDGBufferRef TranslucencyVolumeFilteredVisibleLightHash = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), TranslucencyVolumeVisibleLightHashBufferSize), TEXT("MegaLights.TranslucencyVolume.FilteredVisibleLightHash"));

			FVolumeFilterVisibleLightHashCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FVolumeFilterVisibleLightHashCS::FParameters>();
			PassParameters->RWVisibleLightHash = GraphBuilder.CreateUAV(TranslucencyVolumeFilteredVisibleLightHash);
			PassParameters->MegaLightsParameters = MegaLightsParameters;
			PassParameters->VolumeVisibleLightHashViewSizeInTiles = TranslucencyVolumeVisibleLightHashSizeInTiles;
			PassParameters->VisibleLightHashBuffer = GraphBuilder.CreateSRV(TranslucencyVolumeVisibleLightHash[CascadeIndex]);

			FVolumeFilterVisibleLightHashCS::FPermutationDomain PermutationVector;
			PermutationVector.Set<FVolumeFilterVisibleLightHashCS::FDebugMode>(bDebugPass);
			auto ComputeShader = View.ShaderMap->GetShader<FVolumeFilterVisibleLightHashCS>(PermutationVector);

			const FIntVector GroupCount = FComputeShaderUtils::GetGroupCount(TranslucencyVolumeVisibleLightHashSizeInTiles, FVolumeFilterVisibleLightHashCS::GetGroupSize());

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("TranslucencyVolumeFilterVisibleLightHash"),
				ComputeShader,
				PassParameters,
				GroupCount);

			TranslucencyVolumeVisibleLightHash[CascadeIndex] = TranslucencyVolumeFilteredVisibleLightHash;
		}
	}
}
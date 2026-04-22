// Copyright Epic Games, Inc. All Rights Reserved.

#include "Runtime/Engine/Private/Materials/MaterialIRValueAnalyzer.h"

#if WITH_EDITOR

#include "Materials/MaterialExternalCodeRegistry.h"
#include "Materials/MaterialExpressionDBufferTexture.h"
#include "Materials/MaterialExpressionUtils.h"
#include "Materials/MaterialIRModule.h"
#include "Materials/MaterialIRDebug.h"
#include "Materials/MaterialIR.h"
#include "Materials/MaterialIRTypes.h"
#include "Materials/MaterialIRInternal.h"
#include "Materials/MaterialInsights.h"
#include "Materials/Material.h"
#include "MaterialCachedData.h"
#include "MaterialDomain.h"
#include "MaterialShared.h"
#include "ParameterCollection.h"
#include "Engine/Font.h"
#include "Engine/Texture.h"
#include "Engine/Texture2D.h"
#include "VT/RuntimeVirtualTexture.h"
#include "UObject/Package.h"
#include "PostProcess/PostProcessMaterialInputs.h"

static void ErrorUnlessFeatureLevelSupported(FMaterialIRModule* Module, ERHIFeatureLevel::Type RequiredFeatureLevel, const TCHAR* Message)
{
	ERHIFeatureLevel::Type FeatureLevel = Module->GetFeatureLevel();
	if (FeatureLevel < RequiredFeatureLevel)
	{
		FString FeatureLevelName;
		GetFeatureLevelName(FeatureLevel, FeatureLevelName);
		Module->AddError(nullptr, FString::Printf(TEXT("%s  Current feature level is %s."), Message, *FeatureLevelName));
	}
}

static void AnalyzeExternalInput(FMaterialIRValueAnalyzer& Analyzer, MIR::FExternalInput* ExternalInput)
{
	FMaterialIRModule::FStatistics& Statistics = Analyzer.Module->GetStatistics();
	Statistics.ExternalInputUsedMask[MIR::Stage_Vertex][(int)ExternalInput->Id] = true;
	Statistics.ExternalInputUsedMask[MIR::Stage_Pixel][(int)ExternalInput->Id] = true;

	if (MIR::IsExternalInputTexCoordOrPartialDerivative(ExternalInput->Id))
	{
		int32 TexCoordIndex = MIR::ExternalInputToTexCoordIndex(ExternalInput->Id);
		Statistics.NumVertexTexCoords = FMath::Max(Statistics.NumVertexTexCoords, TexCoordIndex + 1);
		Statistics.NumPixelTexCoords = FMath::Max(Statistics.NumPixelTexCoords, TexCoordIndex + 1);
	}

	if (Analyzer.Material->IsPostProcessMaterial() && MIR::IsExternalInputWorldPosition(ExternalInput->Id))
	{
		Analyzer.CompilationOutput->SetIsSceneTextureUsed(PPI_SceneDepth);
	}

	if (ExternalInput->Id == MIR::EExternalInput::GlobalDistanceField)
	{
		if (!FDataDrivenShaderPlatformInfo::GetSupportsDistanceFields(Analyzer.Module->GetShaderPlatform()))
		{
			FString ShaderPlatformName = FDataDrivenShaderPlatformInfo::GetName(Analyzer.Module->GetShaderPlatform()).ToString();
			Analyzer.Module->AddError(nullptr, FString::Printf(TEXT("Node not supported in shader platform %s. The node requires DistanceField support."), *ShaderPlatformName));
		}

		Analyzer.CompilationOutput->bUsesGlobalDistanceField = true;
	}

	if (ExternalInput->Id == MIR::EExternalInput::DynamicParticleParameterIndex)
	{
		Analyzer.Module->GetStatistics().DynamicParticleParameterMask |= 1 << ExternalInput->UserData;
	}
}

static void AnalyzeInStageHardwarePartialDerivative(FMaterialIRValueAnalyzer& Analyzer, MIR::FHardwarePartialDerivative* PartialDerivative, MIR::EStage Stage)
{
	if (Stage == MIR::Stage_Vertex)
	{
		if (PartialDerivative->Source == MIR::EDerivativeSource::TextureSampleBias)
		{
			Analyzer.Module->AddError(nullptr, TEXT("(Node TextureSample) MipBias is only supported in the pixel shader"));
		}
		else
		{
			Analyzer.Module->AddError(nullptr, FString::Printf(TEXT("Invalid DD%c node used in vertex shader input!"), 'X' + (int32)PartialDerivative->Axis));
		}
	}
}

static EMaterialShaderFrequency MapToMaterialShaderFrequencyOrAny(MIR::EStage Stage)
{
	switch (Stage)
	{
		case MIR::EStage::Stage_Vertex: return EMaterialShaderFrequency::Vertex;
		case MIR::EStage::Stage_Pixel: return EMaterialShaderFrequency::Pixel;
		case MIR::EStage::Stage_Compute: return EMaterialShaderFrequency::Compute;
	}
	return EMaterialShaderFrequency::Any;
}

static void AnalyzeInStageInlineHLSL(FMaterialIRValueAnalyzer& Analyzer, MIR::FInlineHLSL* InlineHLSL, MIR::EStage Stage)
{
	if (!InlineHLSL->HasFlags(MIR::EValueFlags::HasDynamicHLSLCode))
	{
		check(InlineHLSL->ExternalCodeDeclaration != nullptr);
		const EMaterialShaderFrequency StageToMaterialFrequency = MapToMaterialShaderFrequencyOrAny(Stage);
		for (const FMaterialExternalCodeEnvironmentDefine& EnvironmentDefine : InlineHLSL->ExternalCodeDeclaration->EnvironmentDefines)
		{
			if ((int32)(EnvironmentDefine.ShaderFrequency & StageToMaterialFrequency) != 0)
			{
				Analyzer.EnvironmentDefines.Add(EnvironmentDefine.Name);
			}
		}
	}
}

static void AnalyzeInlineHLSL(FMaterialIRValueAnalyzer& Analyzer, MIR::FInlineHLSL* InlineHLSL)
{
	if (!InlineHLSL->HasFlags(MIR::EValueFlags::HasDynamicHLSLCode))
	{
		check(InlineHLSL->ExternalCodeDeclaration != nullptr);

		// Validate this external code can be used for the current material domain. Empty list implies no restriction on material domains.
		if (!InlineHLSL->ExternalCodeDeclaration->Domains.IsEmpty() &&
			InlineHLSL->ExternalCodeDeclaration->Domains.Find(Analyzer.Material->MaterialDomain) == INDEX_NONE)
		{
			FName AssetPathName = Analyzer.Material->GetOutermost()->GetFName();
			Analyzer.Module->AddError(nullptr, MaterialExpressionUtils::FormatUnsupportedMaterialDomainError(*InlineHLSL->ExternalCodeDeclaration, AssetPathName));
		}

		// Cast from material feature level enum to RHI feature level enum
		ERHIFeatureLevel::Type MinimumFeatureLevel = (ERHIFeatureLevel::Type)InlineHLSL->ExternalCodeDeclaration->MinimumFeatureLevel;

		if (Analyzer.Module->GetFeatureLevel() < (ERHIFeatureLevel::Type)InlineHLSL->ExternalCodeDeclaration->MinimumFeatureLevel)
		{
			FString FeatureLevelName;
			FString MinimumFeatureLevelName;
			GetFeatureLevelName(Analyzer.Module->GetFeatureLevel(), FeatureLevelName);
			GetFeatureLevelName(MinimumFeatureLevel, MinimumFeatureLevelName);

			Analyzer.Module->AddError(nullptr, FString::Printf(TEXT("Node %s requires feature level %s.  Current feature level is %s."),
				*InlineHLSL->ExternalCodeDeclaration->Name.ToString(), *MinimumFeatureLevelName, *FeatureLevelName));
		}
	}
}

static int32 AcquireVTStackIndex(FMaterialIRValueAnalyzer& Analyzer, MIR::FVTPageTableRead& VTPageTableRead, bool bGenerateFeedback)
{
	// Try to find matching VT stack entry
	for (int32 VTStackIndex = 0; VTStackIndex < Analyzer.VTStacks.Num(); ++VTStackIndex)
	{
		const FMaterialIRValueAnalyzer::FVTStackEntry& VTStack = Analyzer.VTStacks[VTStackIndex];
		if (VTStack.TexCoord == VTPageTableRead.TexCoord &&
			VTStack.bGenerateFeedback == bGenerateFeedback &&
			VTStack.AddressU == VTPageTableRead.AddressU &&
			VTStack.AddressV == VTPageTableRead.AddressV &&
			VTStack.MipValue == VTPageTableRead.MipValue &&
			VTStack.MipValueMode == VTPageTableRead.MipValueMode)
		{
			return VTStackIndex;
		}
	}

	// Add new VT stack entry
	FMaterialIRValueAnalyzer::FVTStackEntry& VTStack = Analyzer.VTStacks.AddDefaulted_GetRef();
	VTStack.TexCoord = VTPageTableRead.TexCoord;
	VTStack.bGenerateFeedback = bGenerateFeedback;
	VTStack.AddressU = VTPageTableRead.AddressU;
	VTStack.AddressV = VTPageTableRead.AddressV;
	VTStack.MipValue = VTPageTableRead.MipValue;
	VTStack.MipValueMode = VTPageTableRead.MipValueMode;

	return Analyzer.CompilationOutput->UniformExpressionSet.AddVTStack(INDEX_NONE);
}

static int32 RegisterTextureParameter(FMaterialIRValueAnalyzer& Analyzer, UObject* Texture, const FHashedMaterialParameterInfo& ParameterInfo, int32 VTLayerIndex = INDEX_NONE)
{
	check(Texture);
	check(VTLayerIndex == INDEX_NONE || (VTLayerIndex >= 0 && VTLayerIndex < UINT8_MAX));

	const FMaterialTextureParameterInfo TextureParameterInfo
	{
		.ParameterInfo = ParameterInfo,
		.TextureIndex = Analyzer.Material->GetReferencedTextures().Find(Texture),
		.SamplerSource = SSM_FromTextureAsset,
		.VirtualTextureLayerIndex = static_cast<uint8>(VTLayerIndex == INDEX_NONE ? UINT8_MAX : VTLayerIndex),
	};
	check(TextureParameterInfo.TextureIndex != INDEX_NONE);

	const EMaterialTextureParameterType ParamType = MIR::Internal::TextureMaterialValueTypeToParameterType(MIR::Internal::GetTextureMaterialValueType(Texture));

	return Analyzer.CompilationOutput->UniformExpressionSet.FindOrAddTextureParameter(ParamType, TextureParameterInfo);
}

static void AccessTextureObject(FMaterialIRValueAnalyzer& Analyzer, MIR::FTextureObject* TextureObject)
{
	if (TextureObject->Analysis_UniformParameterIndex == INDEX_NONE)
	{
		const FHashedMaterialParameterInfo ParameterInfo{ TextureObject->Texture->GetFName(), EMaterialParameterAssociation::GlobalParameter, INDEX_NONE };
		TextureObject->Analysis_UniformParameterIndex = RegisterTextureParameter(Analyzer, TextureObject->Texture, ParameterInfo, 0);
	}
}

static void AccessRuntimeVirtualTextureObject(FMaterialIRValueAnalyzer& Analyzer, MIR::FRuntimeVirtualTextureObject* RVTextureObject)
{
	if (RVTextureObject->Analysis_UniformParameterIndex == INDEX_NONE)
	{
		const FHashedMaterialParameterInfo ParameterInfo{ RVTextureObject->RVTexture->GetFName(), EMaterialParameterAssociation::GlobalParameter, INDEX_NONE };
		RVTextureObject->Analysis_UniformParameterIndex = RegisterTextureParameter(Analyzer, RVTextureObject->RVTexture, ParameterInfo, RVTextureObject->VTLayerIndex);
	}
}

static void AccessTextureUniformParameter(FMaterialIRValueAnalyzer& Analyzer, MIR::FUniformParameter* UniformParameter)
{
	if (UniformParameter->Analysis_UniformParameterIndex == INDEX_NONE)
	{
		const FMaterialParameterValue& ParameterValue = Analyzer.Module->GetParameterMetadata(UniformParameter->ParameterIdInModule).Value;
		const FHashedMaterialParameterInfo& ParameterInfo = Analyzer.Module->GetParameterInfo(UniformParameter->ParameterIdInModule);
		UniformParameter->Analysis_UniformParameterIndex = RegisterTextureParameter(Analyzer, ParameterValue.AsTextureObject(), ParameterInfo, UniformParameter->VTLayerIndex);
	}
}

static void AccessTexture(FMaterialIRValueAnalyzer& Analyzer, MIR::FValue* TextureValue)
{
	if (MIR::FTextureObject* TextureObject = TextureValue->As<MIR::FTextureObject>())
	{
		AccessTextureObject(Analyzer, TextureObject);
	}
	else if (MIR::FRuntimeVirtualTextureObject* RVTextureObject = TextureValue->As<MIR::FRuntimeVirtualTextureObject>())
	{
		AccessRuntimeVirtualTextureObject(Analyzer, RVTextureObject);
	}
	else if (MIR::FUniformParameter* UniformParameter = TextureValue->As<MIR::FUniformParameter>())
	{
		AccessTextureUniformParameter(Analyzer, UniformParameter);
	}
	else
	{
		UE_MIR_UNREACHABLE();
	}
}

static int32 GetVTLayerIndex(MIR::FValue* TextureObject)
{
	if (MIR::FRuntimeVirtualTextureObject* RVTextureObject = TextureObject->As<MIR::FRuntimeVirtualTextureObject>())
	{
		return RVTextureObject->VTLayerIndex;
	}
	if (MIR::FUniformParameter* UniformParameter = TextureObject->As<MIR::FUniformParameter>())
	{
		return UniformParameter->VTLayerIndex;
	}
	return INDEX_NONE;
}

static int32 GetPageTableLayerIndex(MIR::FValue* TextureObject)
{
	if (MIR::FRuntimeVirtualTextureObject* RVTextureObject = TextureObject->As<MIR::FRuntimeVirtualTextureObject>())
	{
		return RVTextureObject->VTPageTableIndex;
	}
	if (MIR::FUniformParameter* UniformParameter = TextureObject->As<MIR::FUniformParameter>())
	{
		return UniformParameter->VTPageTableIndex;
	}
	return INDEX_NONE;
}

static void AnalyzeInStageVTPageTableRead(FMaterialIRValueAnalyzer& Analyzer, MIR::FVTPageTableRead* VTPageTableRead, MIR::EStage Stage)
{
	check(VTPageTableRead->TextureObject);

	AccessTexture(Analyzer, VTPageTableRead->TextureObject);
	const int32 TextureUniformIndex = VTPageTableRead->TextureObject->GetUniformParameterIndex();
	check(TextureUniformIndex >= 0);

	// Only support GPU feedback from pixel shader
	const bool bGenerateFeedback = VTPageTableRead->bEnableFeedback && Stage == MIR::Stage_Pixel;

	const int32 VTStackIndex = AcquireVTStackIndex(Analyzer, *VTPageTableRead, bGenerateFeedback);

	VTPageTableRead->VTStackIndex = VTStackIndex;

	// Check if VT layer is already known. Otherwise, acquire VT layer
	int32 VTLayerIndex = GetVTLayerIndex(VTPageTableRead->TextureObject);
	if (VTLayerIndex != INDEX_NONE)
	{
		// The layer index in the VT stack is already known, so fetch the page table from the texture object and assign it to the VT stack
		VTPageTableRead->VTPageTableIndex = GetPageTableLayerIndex(VTPageTableRead->TextureObject);
		Analyzer.CompilationOutput->UniformExpressionSet.SetVTLayer(VTStackIndex, VTLayerIndex, TextureUniformIndex);
	}
	else
	{
		VTLayerIndex = Analyzer.CompilationOutput->UniformExpressionSet.GetVTStack(VTStackIndex).FindLayer(TextureUniformIndex);
		if (VTLayerIndex == INDEX_NONE)
		{
			VTLayerIndex = Analyzer.CompilationOutput->UniformExpressionSet.AddVTLayer(VTStackIndex, TextureUniformIndex);
		}
		VTPageTableRead->VTPageTableIndex = VTLayerIndex;
	}
}

static void AnalyzeScreenTexture(FMaterialIRValueAnalyzer& Analyzer, MIR::FScreenTexture* ScreenTexture)
{
	const EMaterialDomain MaterialDomain = Analyzer.Material->MaterialDomain;

	switch (ScreenTexture->TextureKind)
	{
		case MIR::EScreenTexture::SceneTexture:
		case MIR::EScreenTexture::UserSceneTexture:
			{
				// TODO:  If referenced from Custom HLSL, this can be false.  Revisit when custom HLSL support is added to new translator.
				const bool bTextureLookup = true;

				// Add defines and compilation outputs
				Analyzer.EnvironmentDefines.Add(FName(TEXT("NEEDS_SCENE_TEXTURES")));

				Analyzer.CompilationOutput->bNeedsSceneTextures = true;
				Analyzer.CompilationOutput->SetIsSceneTextureUsed(ScreenTexture->Id);

				if (ScreenTexture->TextureKind == MIR::EScreenTexture::UserSceneTexture)
				{
					if (ScreenTexture->UserSceneTexture.IsNone())
					{
						Analyzer.Module->AddError(nullptr, TEXT("UserSceneTexture missing name -- value must be set to something other than None"));
					}
					else
					{
						// Allocate value during analyze, and check for failure.  FindUserSceneTexture is later called during HLSL generation to retrieve the allocated Id.
						int32 SceneTextureId = Analyzer.CompilationOutput->FindOrAddUserSceneTexture(ScreenTexture->UserSceneTexture);
						if (SceneTextureId == INDEX_NONE)
						{
							Analyzer.Module->AddError(nullptr, FString::Printf(TEXT("Too many unique UserSceneTexture inputs in the post process material -- max allowed is %d"), kPostProcessMaterialInputCountMax));
						}
					}
				}

				// Substrate TODO:
				// Substrate only
				// When SceneTexture lookup node is used, single/simple paths are disabled to ensure texture decoding is properly handled.
				// Reading SceneTexture, when Substrate is enabled, implies unpacking material buffer data. The unpacking function exists in different 'flavor' 
				// for optimization purpose (simple/single/complex). To avoid compiling out single or complex unpacking paths (due to defines set by analyzing 
				// the current shader, vs. scene texture pixels), we force Simple/Single versions to be disabled
				// FSubstrateCompilationContext& SubstrateCtx = SubstrateCompilationContext[CurrentSubstrateCompilationContext];
				// SubstrateCtx.SubstrateMaterialComplexity.bIsSimple = false;
				// SubstrateCtx.SubstrateMaterialComplexity.bIsSingle = false;

				// Error checking
				EShaderPlatform Platform = Analyzer.Module->GetShaderPlatform();

				// Error checking from FHLSLMaterialTranslator::SceneTextureLookup
				// Guard against using unsupported textures with SLW
				const bool bHasSingleLayerWaterSM = Analyzer.Material->GetShadingModels().HasShadingModel(MSM_SingleLayerWater);
				if (bHasSingleLayerWaterSM && ScreenTexture->Id != PPI_CustomDepth && ScreenTexture->Id != PPI_CustomStencil)
				{
					Analyzer.Module->AddError(nullptr, TEXT("Only custom depth and custom stencil can be sampled with SceneTexture when used with the Single Layer Water shading model."));
				}

				if (ScreenTexture->Id == PPI_DecalMask)
				{
					Analyzer.Module->AddError(nullptr, TEXT("Decal Mask bit was moved from GBuffer to the Stencil Buffer for performance optimisation so therefore no longer available."));
				}

				// Error checking from FHLSLMaterialTranslator::UseSceneTextureId
				if (MaterialDomain == MD_DeferredDecal)
				{
					const bool bSceneTextureSupportsDecal = ScreenTexture->Id == PPI_SceneDepth || ScreenTexture->Id == PPI_WorldNormal || ScreenTexture->Id == PPI_CustomDepth || ScreenTexture->Id == PPI_CustomStencil;
					if (!bSceneTextureSupportsDecal)
					{
						// Note: For DBuffer decals CustomDepth and CustomStencil are not available if r.CustomDepth.Order = 1
						Analyzer.Module->AddError(nullptr, TEXT("Decals can only access SceneDepth, CustomDepth, CustomStencil, and WorldNormal."));
					}

					const bool bSceneTextureRequiresSM5 = ScreenTexture->Id == PPI_WorldNormal;
					if (bSceneTextureRequiresSM5)
					{
						ErrorUnlessFeatureLevelSupported(Analyzer.Module, ERHIFeatureLevel::SM5, TEXT("Deferred decals require SM5 for World Normal access."));
					}

					if (ScreenTexture->Id == PPI_WorldNormal && !IsUsingDBuffers(Platform))
					{
						bool bHasNormalConnected =
							Substrate::IsSubstrateEnabled() ?
							FSubstrateMaterialInfo::HasPropertyConnected(Analyzer.Material->GetCachedExpressionData().PropertyConnectedMask, MP_Normal) :
							Analyzer.Material->HasNormalConnected();

						if (bHasNormalConnected)
						{
							// GBuffer decals can't bind Normal for read and write.
							// Note: DBuffer decals can support this but only if the sampled WorldNormal isn't connected to the output normal.
							Analyzer.Module->AddError(nullptr, TEXT("Decals that read WorldNormal cannot output to normal at the same time. Enable DBuffer to support this."));
						}
					}
				}

				if (ScreenTexture->Id == PPI_SceneColor && MaterialDomain != MD_Surface)
				{
					if (MaterialDomain == MD_PostProcess)
					{
						Analyzer.Module->AddError(nullptr, TEXT("SceneColor lookups are only available when MaterialDomain = Surface. PostProcessMaterials should use the SceneTexture PostProcessInput0."));
					}
					else
					{
						Analyzer.Module->AddError(nullptr, TEXT("SceneColor lookups are only available when MaterialDomain = Surface."));
					}
				}

				bool bNeedsSceneTexturePostProcessInputs = false;
				if (bTextureLookup)
				{
					bNeedsSceneTexturePostProcessInputs =
						((ScreenTexture->Id >= PPI_PostProcessInput0 && ScreenTexture->Id <= PPI_PostProcessInput6) ||
						 (ScreenTexture->Id >= PPI_UserSceneTexture0 && ScreenTexture->Id <= PPI_UserSceneTexture6) ||
						 ScreenTexture->Id == PPI_Velocity ||
						 ScreenTexture->Id == PPI_SceneColor);
				}

				if ((ScreenTexture->TextureKind == MIR::EScreenTexture::SceneTexture) && ((1u << ScreenTexture->Id) & FMaterialCompilationOutput::GetGBufferMask()))
				{
					if (IsForwardShadingEnabled(Platform) || (IsMobilePlatform(Platform) && !IsMobileDeferredShadingEnabled(Platform)))
					{
						Analyzer.Module->AddError(nullptr, FString::Printf(TEXT("GBuffer scene textures not available with forward shading (platform id %d)."), Platform));
					}

					// Post-process can't access memoryless GBuffer on mobile
					if (IsMobilePlatform(Platform))
					{
						if (MaterialDomain == MD_PostProcess)
						{
							Analyzer.Module->AddError(nullptr, FString::Printf(TEXT("GBuffer scene textures not available in post-processing with mobile shading (platform id %d)."), Platform));
						}

						if (Analyzer.Material->IsMobileSeparateTranslucencyEnabled())
						{
							Analyzer.Module->AddError(nullptr, FString::Printf(TEXT("GBuffer scene textures not available for separate translucency with mobile shading (platform id %d)."), Platform));
						}
					}
				}

				if (ScreenTexture->Id == PPI_Velocity)
				{
					if (MaterialDomain != MD_PostProcess)
					{
						Analyzer.Module->AddError(nullptr, TEXT("Velocity scene textures are only available in post process materials."));
					}
				}

				// Error checking from FHLSLMaterialTranslator::TranslateMaterial
				if (MaterialDomain != MD_DeferredDecal && MaterialDomain != MD_PostProcess)
				{
					if (!Analyzer.Material->GetShadingModels().HasShadingModel(MSM_SingleLayerWater) && IsOpaqueOrMaskedBlendMode(Analyzer.Material->BlendMode))
					{
						// In opaque pass, none of the textures are available
						Analyzer.Module->AddError(nullptr, TEXT("SceneTexture expressions cannot be used in opaque materials except if used with the Single Layer Water shading model."));
					}
					else if (bNeedsSceneTexturePostProcessInputs)
					{
						Analyzer.Module->AddError(nullptr, TEXT("SceneTexture expressions cannot use post process inputs or scene color in non post process domain materials"));
					}
				}

				if (ScreenTexture->Id == PPI_SceneDepth && bTextureLookup)
				{
					// Don't allow opaque and masked materials to access scene depth as the results are undefined
					if (MaterialDomain != MD_PostProcess && !IsTranslucentBlendMode(Analyzer.Material->BlendMode))
					{
						Analyzer.Module->AddError(nullptr, TEXT("Only transparent or postprocess materials can read from scene depth."));
					}
				}
			}
			break;
		case MIR::EScreenTexture::SceneColor:
			{
				// Add defines and compilation outputs
				Analyzer.CompilationOutput->SetIsSceneTextureUsed(PPI_SceneColor);

				// Error checking
				if (MaterialDomain != MD_Surface)
				{
					Analyzer.Module->AddError(nullptr, TEXT("SceneColor lookups are only available when MaterialDomain = Surface."));
				}

				ErrorUnlessFeatureLevelSupported(Analyzer.Module, ERHIFeatureLevel::SM5, TEXT("Scene Color access require SM5."));
			}
			break;
		case MIR::EScreenTexture::SceneDepth:
			{
				// Add defines and compilation outputs
				Analyzer.CompilationOutput->SetIsSceneTextureUsed(PPI_SceneDepth);

				// Error checking
				if (Analyzer.Material->IsTranslucencyWritingVelocity())
				{
					Analyzer.Module->AddError(nullptr, TEXT("Translucenct material with 'Output Velocity' enabled will write to depth buffer, therefore cannot read from depth buffer at the same time."));
				}

				// Don't allow opaque and masked materials to access scene depth as the results are undefined
				if (MaterialDomain != MD_PostProcess && !IsTranslucentBlendMode(Analyzer.Material->BlendMode))
				{
					Analyzer.Module->AddError(nullptr, TEXT("Only transparent or postprocess materials can read from scene depth."));
				}
			}
			break;
		case MIR::EScreenTexture::SceneDepthWithoutWater:
			{
				// Error checking (no defines or compilation outputs needed for SceneDepthWithoutWater)
				if (MaterialDomain != MD_PostProcess)
				{
					if (!Analyzer.Material->GetShadingModels().HasShadingModel(MSM_SingleLayerWater))
					{
						Analyzer.Module->AddError(nullptr, TEXT("Can only read scene depth below water when material Shading Model is Single Layer Water or when material Domain is PostProcess."));
					}

					if (MaterialDomain != MD_Surface)
					{
						Analyzer.Module->AddError(nullptr, TEXT("Can only read scene depth below water when material Domain is set to Surface or PostProcess."));
					}

					if (IsTranslucentBlendMode(Analyzer.Module->GetBlendMode()))
					{
						Analyzer.Module->AddError(nullptr, TEXT("Can only read scene depth below water when material Blend Mode isn't translucent."));
					}
				}
			}
			break;
		case MIR::EScreenTexture::DBufferTexture:
			{
				// Add defines and compilation outputs
				Analyzer.EnvironmentDefines.Add(FName(TEXT("MATERIAL_USES_DECAL_LOOKUP")));

				Analyzer.CompilationOutput->SetIsDBufferTextureUsed(ScreenTexture->DBufferId);
				// set separate flag to indicate that material uses DBuffer lookup specifically
				// can't rely on UsedDBufferTextures because those bits are also set depending on the default decal response behavior.
				Analyzer.CompilationOutput->SetIsDBufferTextureLookupUsed(true);

				// Error checking
				if (MaterialDomain != MD_Surface || IsTranslucentBlendMode(Analyzer.Module->GetBlendMode()))
				{
					Analyzer.Module->AddError(nullptr, TEXT("DBuffer scene textures are only available on opaque or masked surfaces."));
				}
			}
			break;
	}
}

static void AnalyzeInStageScreenTexture(FMaterialIRValueAnalyzer& Analyzer, MIR::FScreenTexture* ScreenTexture, MIR::EStage Stage)
{
	// This function handles stage specific error checking
	switch (ScreenTexture->TextureKind)
	{
		case MIR::EScreenTexture::SceneTexture:
		case MIR::EScreenTexture::UserSceneTexture:
			break;
		case MIR::EScreenTexture::SceneColor:
			if (Stage == MIR::Stage_Vertex)
			{
				Analyzer.Module->AddError(nullptr, TEXT("Scene Color is only supported in pixel shader input!"));
			}
			break;
		case MIR::EScreenTexture::SceneDepth:
			if (Stage == MIR::Stage_Vertex)
			{
				ErrorUnlessFeatureLevelSupported(Analyzer.Module, ERHIFeatureLevel::SM5, TEXT("Reading scene depth from the vertex shader requires SM5."));
			}
			break;
		case MIR::EScreenTexture::SceneDepthWithoutWater:
			if (Stage == MIR::Stage_Vertex)
			{
				// Mobile currently does not support this, we need to read a separate copy of the depth, we must disable framebuffer fetch and force scene texture reads.
				// (Texture bindings are not setup properly for any platform so we're disallowing usage in vertex shader altogether now)
				Analyzer.Module->AddError(nullptr, TEXT("Cannot read scene depth without water from the vertex shader."));
			}
			break;
		case MIR::EScreenTexture::DBufferTexture:
			break;
	}
}

static void AnalyzeShadingModel(FMaterialIRValueAnalyzer& Analyzer, MIR::FShadingModel* ShadingModel)
{
	if (ShadingModel->Id < MSM_NUM)
	{
		Analyzer.Module->AddShadingModel(ShadingModel->Id);
	}
}

static void AnalyzeBranch(FMaterialIRValueAnalyzer& Analyzer, MIR::FBranch* Branch)
{
	Branch->TrueBlock = Analyzer.Module->AllocateArray<MIR::FBlock>(Analyzer.Module->GetNumEntryPoints());
	MIR::ZeroArray(Branch->TrueBlock);

	Branch->FalseBlock = Analyzer.Module->AllocateArray<MIR::FBlock>(Analyzer.Module->GetNumEntryPoints());
	MIR::ZeroArray(Branch->FalseBlock);
}

static void AnalyzeTextureRead(FMaterialIRValueAnalyzer& Analyzer, MIR::FTextureRead* TextureRead)
{
	// Ensure a uniform parameter is allocated when a texture read instruction accesses the texture.
	// Otherwise, no uniform parameter must be allocated for the texture object in case it's used for other nodes only such as texture properties.
	AccessTexture(Analyzer, TextureRead->TextureObject);
}

// Returns the next available offset into the preshader buffer for a float vector with the specified number of components (1-4).
static uint32 NextGlobalComponentOffset(FMaterialIRValueAnalyzer& Analyzer, const MIR::FPrimitive& PrimitiveType)
{
	const uint32 NumComponents = PrimitiveType.NumComponents();
	const bool bAreComponentsDouble = PrimitiveType.IsDouble();

	// Following code calculates GlobalComponentOffset.
	// GlobalComponentOffset is the i-th component in the array of float4s that make the uniform buffer.
	// For example a GlobalComponentOffset of 13 references PreshaderBuffer[3].y.
	// First, try to find an available sequence of free components in any previous allocation, in
	// order to reduce the number of allocations and thus preshader buffer memory footprint.
	// If the parameter type is too large and we can't find space for it in previous allocations,
	// allocate a new uniform buffer slot (a float4, 16 bytes) and put any unused component
	// in the appropriate freelist.

	uint32 GlobalComponentOffset = UINT32_MAX;
	check(NumComponents <= 4); // Only vectors supported for now

	uint32 UsedNumComponents;

	for (UsedNumComponents = NumComponents; UsedNumComponents < 4; ++UsedNumComponents)
	{
		auto& FreeOffsetsForCurrentNumComponents = Analyzer.FreeOffsetsPerNumComponents[UsedNumComponents - 1];

		// If there are no available slots at this nubmer of components, try with a larger one
		if (FreeOffsetsForCurrentNumComponents.IsEmpty())
		{
			continue;
		}

		// There is space for this number `NumComponents` in a a chunk with `i` free components
		GlobalComponentOffset = FreeOffsetsForCurrentNumComponents.Last();
		FreeOffsetsForCurrentNumComponents.Pop();

		break;
	}

	// If UsedNumComponents is 4, it means we looked for all possible components sizes (1, 2 and 3) and
	// could not find a chunk that would fit this parameter. Allocate a new chunk.
	if (UsedNumComponents >= 4)
	{
		FUniformExpressionSet& UniformExpressionSet = Analyzer.CompilationOutput->UniformExpressionSet;
		check(UsedNumComponents == 4);
		const uint32 NumOfFloat4s = bAreComponentsDouble ? 2 : 1;
		GlobalComponentOffset = UniformExpressionSet.AllocateFromUniformBuffer(NumOfFloat4s) * 4;
	}

	// Finally, check whether in the used slot there's any slack left, and if so, record it in the appropriate free-list.
	int32 NumComponentsLeft = UsedNumComponents - NumComponents;
	if (NumComponentsLeft > 0)
	{
		int32 LeftOverOffset = GlobalComponentOffset + NumComponents;
		Analyzer.FreeOffsetsPerNumComponents[NumComponentsLeft - 1].Push(LeftOverOffset);
	}

	return GlobalComponentOffset;
}

static uint32 FindOrAddDefaultValueOffset(FMaterialIRValueAnalyzer& Analyzer, const FMaterialParameterValue& ParameterValue)
{
	UE::Shader::FValue DefaultValue;
	switch (ParameterValue.Type)
	{
		case EMaterialParameterType::Scalar: DefaultValue = ParameterValue.AsScalar(); break;
		case EMaterialParameterType::Vector: DefaultValue = ParameterValue.AsLinearColor(); break;
		case EMaterialParameterType::DoubleVector: DefaultValue = ParameterValue.AsVector4d(); break;
		default: UE_MIR_TODO();
	}

	uint32 DefaultValueOffset = 0;
	if (!MIR::Internal::Find(Analyzer.UniformDefaultValueOffsets, DefaultValue, DefaultValueOffset))
	{
		FUniformExpressionSet& UniformExpressionSet = Analyzer.CompilationOutput->UniformExpressionSet;
		DefaultValueOffset = UniformExpressionSet.AddDefaultParameterValue(DefaultValue);
		Analyzer.UniformDefaultValueOffsets.Add(DefaultValue, DefaultValueOffset);
	}

	return DefaultValueOffset;
}

static void AnalyzePrimitiveUniformParameter(FMaterialIRValueAnalyzer& Analyzer, MIR::FUniformParameter* Parameter)
{
	FUniformExpressionSet& UniformExpressionSet = Analyzer.CompilationOutput->UniformExpressionSet;
	FMaterialParameterInfo ParameterInfo = Analyzer.Module->GetParameterInfo(Parameter->ParameterIdInModule);
	FMaterialParameterMetadata ParameterMetadata = Analyzer.Module->GetParameterMetadata(Parameter->ParameterIdInModule);

	// TODO: GetParameterOverrideValueForCurrentFunction?

	const uint32 DefaultValueOffset = FindOrAddDefaultValueOffset(Analyzer, ParameterMetadata.Value);

	Parameter->Analysis_UniformParameterIndex = UniformExpressionSet.FindOrAddNumericParameter(ParameterMetadata.Value.Type, ParameterInfo, DefaultValueOffset);

	// Make sure the parameter type is primitive
	MIR::FPrimitive PrimitiveType = Parameter->Type.GetPrimitive();

	// Only int, float and LWC parameters supported for now
	check(PrimitiveType.IsInteger() || PrimitiveType.IsAnyFloat());

	const uint32 GlobalComponentOffset = NextGlobalComponentOffset(Analyzer, PrimitiveType);

	// Add the parameter evaluation to the uniform data
	UniformExpressionSet.AddNumericParameterEvaluation(Parameter->Analysis_UniformParameterIndex, GlobalComponentOffset);

	if (Analyzer.Insights)
	{
		// Push information about this parameter allocation to the insights.
		FMaterialInsights::FUniformParameterAllocationInsight ParamInsight;
		ParamInsight.BufferSlotIndex = GlobalComponentOffset / 4;
		ParamInsight.BufferSlotOffset = GlobalComponentOffset % 4;
		ParamInsight.ComponentsCount = PrimitiveType.NumComponents();
		ParamInsight.ParameterName = ParameterInfo.Name;

		switch (PrimitiveType.ScalarKind)
		{
			case MIR::EScalarKind::Int:   ParamInsight.ComponentType = FMaterialInsights::FUniformBufferSlotComponentType::CT_Int; break;
			case MIR::EScalarKind::Float: ParamInsight.ComponentType = FMaterialInsights::FUniformBufferSlotComponentType::CT_Float; break;
			case MIR::EScalarKind::Double:   ParamInsight.ComponentType = FMaterialInsights::FUniformBufferSlotComponentType::CT_LWC; break;
			default: UE_MIR_UNREACHABLE();
		}

		Analyzer.Insights->UniformParameterAllocationInsights.Push(ParamInsight);
	}
}

static void AnalyzeUniformParameter(FMaterialIRValueAnalyzer& Analyzer, MIR::FUniformParameter* Parameter)
{
	if (!Parameter->Type.IsTexture())
	{
		check(Parameter->Type.AsPrimitive());
		AnalyzePrimitiveUniformParameter(Analyzer, Parameter);
	}
}

static void AnalyzePreshaderParameter(FMaterialIRValueAnalyzer& Analyzer, MIR::FPreshaderParameter* Parameter)
{
	FUniformExpressionSet& UniformExpressionSet = Analyzer.CompilationOutput->UniformExpressionSet;

	// Get parameter name from source parameter
	FName SourceParameterName;
	if (MIR::FUniformParameter* UniformSourceParameter = Parameter->SourceParameter->As<MIR::FUniformParameter>())
	{
		SourceParameterName = Analyzer.Module->GetParameterInfo(UniformSourceParameter->ParameterIdInModule).Name;
	}
	else if (MIR::FTextureObject* TextureObject = Parameter->SourceParameter->As<MIR::FTextureObject>())
	{
		SourceParameterName = TextureObject->Texture->GetFName();
	}
	else
	{
		SourceParameterName = NAME_None;
	}

	// Make sure the parameter type is primitive
	MIR::FPrimitive PrimitiveType = Parameter->Type.GetPrimitive();

	// Only int, float and LWC parameters supported for now
	check(PrimitiveType.IsInteger() || PrimitiveType.IsAnyFloat());

	const uint32 GlobalComponentOffset = NextGlobalComponentOffset(Analyzer, PrimitiveType);

	UniformExpressionSet.WriteUniformPreshaderEntry(
		GlobalComponentOffset, UE::Shader::MakeValueType(UE::Shader::EValueComponentType::Float, PrimitiveType.NumComponents()),
		[Parameter, SourceParameterName](UE::Shader::FPreshaderData& UniformPreshaderData) -> void
		{
			const FHashedMaterialParameterInfo HashedSourceParameterInfo{ SourceParameterName, EMaterialParameterAssociation::GlobalParameter, INDEX_NONE };

			switch (Parameter->Opcode)
			{
			case UE::Shader::EPreshaderOpcode::TextureSize:
			case UE::Shader::EPreshaderOpcode::TexelSize:
				UniformPreshaderData.WriteOpcode(Parameter->Opcode).Write(HashedSourceParameterInfo).Write(Parameter->TextureIndex);
				break;

			case UE::Shader::EPreshaderOpcode::RuntimeVirtualTextureUniform:
				UniformPreshaderData.WriteOpcode(Parameter->Opcode).Write(HashedSourceParameterInfo).Write(Parameter->TextureIndex).Write(Parameter->Payload.UniformIndex);
				break;

			default:
				UE_MIR_UNREACHABLE();
			}
		}
	);

	Parameter->Analysis_PreshaderOffset = GlobalComponentOffset;
}

static void AnalyzeSetMaterialOutput(FMaterialIRValueAnalyzer& Analyzer, MIR::FSetMaterialOutput* SetMaterialOutput)
{
	if (SetMaterialOutput->Property == EMaterialProperty::MP_Normal)
	{
		if (SetMaterialOutput->HasSubgraphProperties(MIR::EGraphProperties::ReadsPixelNormal))
		{
			Analyzer.Module->AddError(nullptr, TEXT("Cannot set material attribute Normal to a value that depends on reading the pixel normal, as that would create a circular dependency."));
		}
	}
}

static void AnalyzeMaterialParameterCollection(FMaterialIRValueAnalyzer& Analyzer, MIR::FMaterialParameterCollection* MaterialParameterCollection)
{
	int32 CollectionIndex = Analyzer.Module->FindOrAddParameterCollection(MaterialParameterCollection->Collection);
		
	if (CollectionIndex == INDEX_NONE)
	{
		Analyzer.Module->AddError(nullptr, TEXT("Material references too many MaterialParameterCollections!  A material may only reference 2 different collections."));
	}

	MaterialParameterCollection->Analysis_CollectionIndex = CollectionIndex;
}

//////////////////////////////////////////////////////////////////////////////////////////
// FMaterialIRValueAnalyzer
//////////////////////////////////////////////////////////////////////////////////////////

void FMaterialIRValueAnalyzer::Setup(UMaterial* InMaterial, FMaterialIRModule* InModule, FMaterialCompilationOutput* InCompilationOutput, FMaterialInsights* InInsights)
{
	Material = InMaterial;
	Module = InModule;
	Insights = InInsights;
	CompilationOutput = InCompilationOutput;
	UniformDefaultValueOffsets = {};
	FreeOffsetsPerNumComponents[0] = {};
	FreeOffsetsPerNumComponents[1] = {};
	FreeOffsetsPerNumComponents[2] = {};
	EnvironmentDefines = {};
	VTStacks = {};
}

void FMaterialIRValueAnalyzer::Analyze(MIR::FValue* Value)
{
	#define EXPAND_CASE(ValueType, ...) \
		case MIR::VK_##ValueType: \
		{ \
			Analyze##ValueType(*this, static_cast<MIR::F##ValueType*>(Value) , ## __VA_ARGS__); \
			break; \
		}

	// IMPORTANT: Before adding a case here, read the FMaterialIRValueAnalyzer documentation.
	switch (Value->Kind)
	{
		EXPAND_CASE(ExternalInput);
		EXPAND_CASE(MaterialParameterCollection);
		EXPAND_CASE(UniformParameter);
		EXPAND_CASE(Branch);
		EXPAND_CASE(TextureRead);
		EXPAND_CASE(PreshaderParameter);
		EXPAND_CASE(SetMaterialOutput);
		EXPAND_CASE(InlineHLSL);
		EXPAND_CASE(ScreenTexture);
		EXPAND_CASE(ShadingModel);
		default: break;
	}

	#undef EXPAND_CASE
}

void FMaterialIRValueAnalyzer::AnalyzeInStage(MIR::FValue* Value, MIR::EStage Stage)
{
	#define EXPAND_CASE(ValueType) \
	case MIR::VK_##ValueType: \
	{ \
		AnalyzeInStage##ValueType(*this, static_cast<MIR::F##ValueType*>(Value), Stage); \
		break; \
	}

	// IMPORTANT: Before adding a case here, read the FMaterialIRValueAnalyzer documentation.
	switch (Value->Kind)
	{
		EXPAND_CASE(InlineHLSL);
		EXPAND_CASE(VTPageTableRead);
		EXPAND_CASE(ScreenTexture);
		EXPAND_CASE(HardwarePartialDerivative);
		default: break;
	}

	#undef EXPAND_CASE
}
	
#endif // #if WITH_EDITOR

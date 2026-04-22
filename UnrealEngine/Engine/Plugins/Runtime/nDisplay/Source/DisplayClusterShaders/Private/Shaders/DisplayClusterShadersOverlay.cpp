// Copyright Epic Games, Inc. All Rights Reserved.

#include "Shaders/DisplayClusterShadersOverlay.h"
#include "ShaderParameters/DisplayClusterShaderParameters_Overlay.h"

#include "GlobalShader.h"
#include "PixelFormat.h"
#include "PixelShaderUtils.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RHIResources.h"
#include "RHIStaticStates.h"
#include "SceneView.h"
#include "ScreenPass.h"
#include "TextureResource.h"

#include "DisplayClusterShadersLog.h"


namespace UE::DisplayClusterShaders::Private
{
	static const FString OverlayShadersPath = TEXT("/Plugin/nDisplay/Private/OverlayShaders.usf");

	///////////////////////////////////////////////////////////////////////////////////
	// DrawOverlay PS
	class FDrawOverlayPS : public FGlobalShader
	{
		DECLARE_SHADER_TYPE(FDrawOverlayPS, Global);
		SHADER_USE_PARAMETER_STRUCT(FDrawOverlayPS, FGlobalShader);

		BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
			SHADER_PARAMETER_RDG_TEXTURE(Texture2D, TextureBase)
			SHADER_PARAMETER_RDG_TEXTURE(Texture2D, TextureOverlay)
			SHADER_PARAMETER_SAMPLER(SamplerState, Sampler)
			RENDER_TARGET_BINDING_SLOTS()
		END_SHADER_PARAMETER_STRUCT()

	public:

		/** A helper function to initialize shader parameters based on the draw request data */
		FParameters* AllocateAndSetParameters(FRDGBuilder& InGraphBuilder, const FDisplayClusterShaderParameters_Overlay& InParameters)
		{
			FParameters* Parameters = InGraphBuilder.AllocParameters<FParameters>();

			// Input
			Parameters->TextureBase    = InParameters.BaseTexture;
			Parameters->TextureOverlay = InParameters.OverlayTexture;
			Parameters->Sampler        = TStaticSamplerState<SF_Point>::GetRHI();

			// Output
			Parameters->RenderTargets[0] = FRenderTargetBinding{ InParameters.OutputTexture, ERenderTargetLoadAction::ENoAction };

			return Parameters;
		}
	};

	IMPLEMENT_SHADER_TYPE(, FDrawOverlayPS, *OverlayShadersPath, TEXT("DrawOverlay_PS"), SF_Pixel);
}


void FDisplayClusterShadersOverlay::AddOverlayBlendingPass(FRDGBuilder& GraphBuilder, const FDisplayClusterShaderParameters_Overlay& InParameters)
{
	using namespace UE::DisplayClusterShaders::Private;

	// Nothing to do if wrong input
	if (!InParameters.IsValidData())
	{
		return;
	}

	// Initialize shaders
	const FGlobalShaderMap* const GlobalShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
	const TShaderMapRef<FScreenPassVS>  VertexShader(GlobalShaderMap);
	const TShaderMapRef<FDrawOverlayPS> PixelShader(GlobalShaderMap);

	// Instantiate PS shader params
	FDrawOverlayPS::FParameters* ShaderParams = PixelShader->AllocateAndSetParameters(GraphBuilder, InParameters);

	// Add draw pass
	AddDrawScreenPass(
		GraphBuilder,
		RDG_EVENT_NAME("nDisplay.DrawOverlay_PS"),
		FScreenPassViewInfo(),
		FScreenPassTextureViewport{ InParameters.BaseTexture,    FIntRect({0, 0}, InParameters.BaseTexture->Desc.Extent) },
		FScreenPassTextureViewport{ InParameters.OverlayTexture, FIntRect({0, 0}, InParameters.OverlayTexture->Desc.Extent) },
		VertexShader,
		PixelShader,
		ShaderParams
	);
}

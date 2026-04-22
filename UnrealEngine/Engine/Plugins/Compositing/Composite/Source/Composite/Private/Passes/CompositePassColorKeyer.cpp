// Copyright Epic Games, Inc. All Rights Reserved.

#include "Passes/CompositePassColorKeyer.h"

#include "Engine/Texture.h"
#include "Passes/CompositeCorePassProxy.h"
#include "PixelShaderUtils.h"
#include "SystemTextures.h"
#include "TextureResource.h"

#if WITH_EDITOR
#include "Application/ThrottleManager.h"
#endif

DECLARE_GPU_STAT_NAMED(FCompositeDenoise, TEXT("Composite.Denoise"));
DECLARE_GPU_STAT_NAMED(FCompositeColorKeyer, TEXT("Composite.ColorKeyer"));

class FCompositePassDenoiseShader : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FCompositePassDenoiseShader);
	SHADER_USE_PARAMETER_STRUCT(FCompositePassDenoiseShader, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, InputTexture)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, RWOutputTexture)
		SHADER_PARAMETER(FIntPoint, Dimensions)
	END_SHADER_PARAMETER_STRUCT()

	static inline void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("TILE_SIZE"), ThreadGroupSize);

		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}

	static const uint32 ThreadGroupSize = 16;
};

class FCompositePassColorKeyerShader : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FCompositePassColorKeyerShader);
	SHADER_USE_PARAMETER_STRUCT(FCompositePassColorKeyerShader, FGlobalShader);

	class FUseCleanPlate : SHADER_PERMUTATION_BOOL("USE_CLEAN_PLATE");
	using FPermutationDomain = TShaderPermutationDomain<FUseCleanPlate>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, InputTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, InputSampler)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, CleanPlateTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, CleanPlateSampler)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, RWOutputTexture)
		SHADER_PARAMETER_STRUCT(FScreenPassTextureViewportParameters, Input)
		SHADER_PARAMETER_STRUCT(FScreenPassTextureViewportParameters, Output)
		SHADER_PARAMETER(FVector3f, KeyColor)
		SHADER_PARAMETER(FVector4f, Params0)
		SHADER_PARAMETER(FVector4f, Params1)
		SHADER_PARAMETER(uint32, ScreenType)
		SHADER_PARAMETER(uint32, Visualization)
		SHADER_PARAMETER(uint32, bPreserveVignetteAfterKey)
		SHADER_PARAMETER(uint32, bInvertAlpha)
	END_SHADER_PARAMETER_STRUCT()

	static inline void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("TILE_SIZE"), ThreadGroupSize);

		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}

	static const uint32 ThreadGroupSize = 16;
};

IMPLEMENT_GLOBAL_SHADER(FCompositePassDenoiseShader, "/Plugin/Composite/Private/CompositeDenoise.usf", "MainCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FCompositePassColorKeyerShader, "/Plugin/Composite/Private/CompositeColorKeyer.usf", "MainCS", SF_Compute);

namespace UE
{
	namespace CompositeCore
	{
		class FCompositePassColorKeyerProxy : public FCompositeCorePassProxy
		{
		public:
			IMPLEMENT_COMPOSITE_PASS(FCompositePassColorKeyerProxy);

			using FCompositeCorePassProxy::FCompositeCorePassProxy;

			FScreenPassTexture AddDenoisePass(FRDGBuilder& GraphBuilder, const FScreenPassTexture& Input, ERHIFeatureLevel::Type FeatureLevel) const
			{
				RDG_EVENT_SCOPE_STAT(GraphBuilder, FCompositeDenoise, "Composite.Denoise");
				RDG_GPU_STAT_SCOPE(GraphBuilder, FCompositeDenoise);

				FGlobalShaderMap* GlobalShaderMap = GetGlobalShaderMap(FeatureLevel);
				const FIntPoint TextureSize = Input.Texture->Desc.Extent;

				const FRDGTextureDesc OutputDesc = FRDGTextureDesc::Create2D(TextureSize, PF_FloatRGBA, FClearValueBinding::Black, TexCreate_UAV | TexCreate_ShaderResource | TexCreate_RenderTargetable);
				FScreenPassRenderTarget Output = FScreenPassRenderTarget(GraphBuilder.CreateTexture(OutputDesc, TEXT("CompositeTextureDenoised")), ERenderTargetLoadAction::ENoAction);

				FCompositePassDenoiseShader::FParameters* PassParameters = GraphBuilder.AllocParameters<FCompositePassDenoiseShader::FParameters>();
				PassParameters->InputTexture = Input.Texture;
				PassParameters->RWOutputTexture = GraphBuilder.CreateUAV(Output.Texture);
				PassParameters->Dimensions = TextureSize;

				TShaderMapRef<FCompositePassDenoiseShader> ComputeShader(GlobalShaderMap);
				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME("CompositeCore.Denoise (%dx%d)", TextureSize.X, TextureSize.Y),
					ERDGPassFlags::Compute,
					ComputeShader,
					PassParameters,
					FComputeShaderUtils::GetGroupCount(TextureSize, FCompositePassDenoiseShader::ThreadGroupSize)
				);

				return Output;
			}

			FPassTexture Add(FRDGBuilder& GraphBuilder, const FSceneView& InView, const FPassInputArray& Inputs, const FPassContext& PassContext) const override
			{
				RDG_EVENT_SCOPE_STAT(GraphBuilder, FCompositeColorKeyer, "Composite.ColorKeyer");
				RDG_GPU_STAT_SCOPE(GraphBuilder, FCompositeColorKeyer);

				check(ValidateInputs(Inputs));

				const UE::CompositeCore::FResourceMetadata& Metadata = Inputs[0].Metadata;
				FScreenPassTexture Input = Inputs[0].Texture;
				FScreenPassRenderTarget Output = Inputs.OverrideOutput;

				if (DenoiseMethod != ECompositeDenoiseMethod::None)
				{
					Input = AddDenoisePass(GraphBuilder, Input, InView.GetFeatureLevel());
				}

				// If the override output is provided, it means that this is the last pass in post processing.
				if (!Output.IsValid())
				{
					Output = CreateOutputRenderTarget(GraphBuilder, InView, PassContext.OutputViewRect, Input.Texture->Desc, TEXT("ColorKeyerCompositePass"));
				}

				FGlobalShaderMap* GlobalShaderMap = GetGlobalShaderMap(InView.GetFeatureLevel());

				const bool bSameSize = Output.ViewRect.Size() == Input.ViewRect.Size();
				const FScreenPassTextureViewportParameters InputParameters = GetScreenPassTextureViewportParameters(FScreenPassTextureViewport(Input));
				const FScreenPassTextureViewportParameters OutputParameters = GetScreenPassTextureViewportParameters(FScreenPassTextureViewport(Output));

				FCompositePassColorKeyerShader::FParameters* PassParameters = GraphBuilder.AllocParameters<FCompositePassColorKeyerShader::FParameters>();
				PassParameters->InputTexture = Input.Texture;
				PassParameters->InputSampler = bSameSize ? TStaticSamplerState<SF_Point>::GetRHI() : TStaticSamplerState<SF_Bilinear>::GetRHI();
				PassParameters->RWOutputTexture = GraphBuilder.CreateUAV(Output.Texture);
				PassParameters->Input = InputParameters;
				PassParameters->Output = OutputParameters;
				PassParameters->KeyColor = FVector3f(KeyColor);
				PassParameters->Params0 = Params0;
				PassParameters->Params1 = Params1;
				PassParameters->ScreenType = static_cast<uint32>(ScreenType);
				PassParameters->Visualization = static_cast<uint32>(Visualization);
				PassParameters->bPreserveVignetteAfterKey = static_cast<uint32>(bPreserveVignetteAfterKey);
				PassParameters->bInvertAlpha = static_cast<uint32>(bInvertAlpha);
	
				FRDGTextureRef CleanPlateResource = nullptr;
				PassParameters->CleanPlateTexture = GSystemTextures.GetBlackDummy(GraphBuilder);
				PassParameters->CleanPlateSampler = TStaticSamplerState<SF_Bilinear>::GetRHI();

				TStrongObjectPtr<UTexture> CleanPlateTexturePtr = CleanPlateWeakPtr.Pin();
				if (CleanPlateTexturePtr.IsValid())
				{
					const FTextureResource* TextureResource = CleanPlateTexturePtr->GetResource();
					if (TextureResource != nullptr)
					{
						FRHITexture* TextureRHI = TextureResource->GetTexture2DRHI();
						if (TextureRHI != nullptr)
						{
							CleanPlateResource = GraphBuilder.RegisterExternalTexture(CreateRenderTarget(TextureRHI, TEXT("CompositeCleanPlateTexture")));
							GraphBuilder.UseInternalAccessMode(CleanPlateResource);

							PassParameters->CleanPlateTexture = CleanPlateResource;
						}
					}
				}

				FCompositePassColorKeyerShader::FPermutationDomain PermutationVector;
				PermutationVector.Set<FCompositePassColorKeyerShader::FUseCleanPlate>(CleanPlateResource != nullptr);

				const FIntPoint OutputSize = Output.ViewRect.Size();
				TShaderMapRef<FCompositePassColorKeyerShader> ComputeShader(GlobalShaderMap, PermutationVector);
				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME("Composite.ColorKeyer (%dx%d)", OutputSize.X, OutputSize.Y),
					ERDGPassFlags::Compute,
					ComputeShader,
					PassParameters,
					FComputeShaderUtils::GetGroupCount(OutputSize, FCompositePassColorKeyerShader::ThreadGroupSize)
				);

				if (CleanPlateResource != nullptr)
				{
					GraphBuilder.UseExternalAccessMode(CleanPlateResource, ERHIAccess::SRVMask);
				}

				return UE::CompositeCore::FPassTexture{ MoveTemp(Output), Metadata };
			}

			ECompositeColorKeyerScreenType ScreenType;
			FLinearColor KeyColor;
			FVector4f Params0;
			FVector4f Params1;
			ECompositeColorKeyerVisualization Visualization;
			bool bPreserveVignetteAfterKey;
			ECompositeDenoiseMethod DenoiseMethod;
			bool bInvertAlpha;
			TWeakObjectPtr<UTexture> CleanPlateWeakPtr;
		};
	}
}

UCompositePassColorKeyer::UCompositePassColorKeyer(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, ScreenType(ECompositeColorKeyerScreenType::Green)
	, KeyColor(FLinearColor::Green)
	, CleanPlate(nullptr)
	, RedWeight(0.5f)
	, GreenWeight(0.5f)
	, BlueWeight(0.5f)
	, AlphaThreshold(0.0f, 1.0f)
	, DespillStrength(0.0f)
	, DevignetteStrength(0.0f)
	, bPreserveVignetteAfterKey(true)
	, DenoiseMethod(ECompositeDenoiseMethod::None)
	, Visualization(ECompositeColorKeyerVisualization::Key)
	, bInvertAlpha(false)
{ 
}

UCompositePassColorKeyer::~UCompositePassColorKeyer() = default;

bool UCompositePassColorKeyer::GetProxy(const UE::CompositeCore::FPassInputDecl& InputDecl, FSceneRenderingBulkObjectAllocator& InFrameAllocator, FCompositeCorePassProxy*& OutProxy) const
{
	using namespace UE::CompositeCore;

	FCompositePassColorKeyerProxy* Proxy = InFrameAllocator.Create<FCompositePassColorKeyerProxy>(UE::CompositeCore::FPassInputDeclArray{ InputDecl });
	Proxy->KeyColor = KeyColor;
	Proxy->Params0 = FVector4f(RedWeight, GreenWeight, BlueWeight, DespillStrength);
	Proxy->Params1 = FVector4f(AlphaThreshold.X, AlphaThreshold.Y, DevignetteStrength, 0.0f);
	Proxy->ScreenType = ScreenType;
	Proxy->bPreserveVignetteAfterKey = bPreserveVignetteAfterKey;
	Proxy->DenoiseMethod = DenoiseMethod;
	Proxy->bInvertAlpha = bInvertAlpha;
	Proxy->Visualization = Visualization;
	Proxy->CleanPlateWeakPtr = CleanPlate;

	OutProxy = Proxy;
	return true;
}

#if WITH_EDITOR
void UCompositePassColorKeyer::PreEditChange(FProperty* PropertyThatWillChange)
{
	Super::PreEditChange(PropertyThatWillChange);

	if (!PropertyThatWillChange)
	{
		return;
	}

	const FName PropertyName = PropertyThatWillChange->GetFName();
	const bool bIsBooleanProperty = CastField<const FBoolProperty>(PropertyThatWillChange) != nullptr;

	if (PropertyName != GET_MEMBER_NAME_CHECKED(ThisClass, bIsEnabled)
		&& PropertyName != GET_MEMBER_NAME_CHECKED(ThisClass, ScreenType)
		&& PropertyName != GET_MEMBER_NAME_CHECKED(ThisClass, KeyColor)
		&& PropertyName != GET_MEMBER_NAME_CHECKED(ThisClass, CleanPlate)
		&& !bIsBooleanProperty
		)
	{
		/**
		* For properties not included in the list above, we want to keep the editor ticking so that
		* result from sliding their (SNumericEntryBox) values can be observed in real-time.
		* 
		* Unlike component transform customizations which prevent throttling explicitly
		* (see .PreventThrottling(true)) we don't appear to have a metadata property for this.
		* 
		* As an alternative solution we directly disable throttling from the slate manager,
		* and re-enable it once the value is set.
		*/
		FSlateThrottleManager::Get().DisableThrottle(true);
		bEditorThrottleDisabled = true;
	}
}

void UCompositePassColorKeyer::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (bEditorThrottleDisabled && PropertyChangedEvent.ChangeType != EPropertyChangeType::Interactive)
	{
		FSlateThrottleManager::Get().DisableThrottle(false);
		bEditorThrottleDisabled = false;
	}
}
#endif


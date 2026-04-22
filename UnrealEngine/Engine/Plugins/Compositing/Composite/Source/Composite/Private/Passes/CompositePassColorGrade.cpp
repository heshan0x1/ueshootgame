// Copyright Epic Games, Inc. All Rights Reserved.

#include "Passes/CompositePassColorGrade.h"

#include "SceneManagement.h"
#include "ScreenPass.h"
#include "PostProcess/PostProcessMaterialInputs.h"
#include "Passes/CompositeCorePassProxy.h"

DECLARE_GPU_STAT_NAMED(FCompositeColorGrade, TEXT("Composite.ColorGrade"));

BEGIN_SHADER_PARAMETER_STRUCT(FColorGradePerRangeParameters, )
	SHADER_PARAMETER(FVector4f, Saturation)
	SHADER_PARAMETER(FVector4f, Contrast)
	SHADER_PARAMETER(FVector4f, Gamma)
	SHADER_PARAMETER(FVector4f, Gain)
	SHADER_PARAMETER(FVector4f, Offset)
END_SHADER_PARAMETER_STRUCT()

class FCompositeColorGradeShader : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FCompositeColorGradeShader);
	SHADER_USE_PARAMETER_STRUCT(FCompositeColorGradeShader, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, InputTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, InputSampler)
		SHADER_PARAMETER_STRUCT_REF(FWorkingColorSpaceShaderParameters, WorkingColorSpace)
		SHADER_PARAMETER_STRUCT(FColorGradePerRangeParameters, Global)
		SHADER_PARAMETER_STRUCT(FColorGradePerRangeParameters, Shadows)
		SHADER_PARAMETER_STRUCT(FColorGradePerRangeParameters, Midtones)
		SHADER_PARAMETER_STRUCT(FColorGradePerRangeParameters, Highlights)
		SHADER_PARAMETER(float, ShadowsMax)
		SHADER_PARAMETER(float, HighlightsMin)
		SHADER_PARAMETER(float, HighlightsMax)
		SHADER_PARAMETER(uint32, bIsTemperatureWhiteBalance)
		SHADER_PARAMETER(float, WhiteTemp)
		SHADER_PARAMETER(float, WhiteTint)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static FColorGradePerRangeParameters ToParameters(const FColorGradePerRangeSettings& InParams)
	{
		FColorGradePerRangeParameters Output;
		Output.Saturation = FVector4f(InParams.Saturation);
		Output.Contrast = FVector4f(InParams.Contrast);
		Output.Gamma = FVector4f(InParams.Gamma);
		Output.Gain = FVector4f(InParams.Gain);
		Output.Offset = FVector4f(InParams.Offset);

		return Output;
	}
};
IMPLEMENT_GLOBAL_SHADER(FCompositeColorGradeShader, "/Plugin/Composite/Private/CompositeColorGrade.usf", "MainPS", SF_Pixel);

class FColorGradeCompositePassProxy : public FCompositeCorePassProxy
{
public:
	IMPLEMENT_COMPOSITE_PASS(FColorGradeCompositePassProxy);

	FColorGradeCompositePassProxy(UE::CompositeCore::FPassInputDeclArray InPassDeclaredInputs)
		: FCompositeCorePassProxy(MoveTemp(InPassDeclaredInputs))
	{ }

	UE::CompositeCore::FPassTexture Add(FRDGBuilder& GraphBuilder, const FSceneView& InView, const UE::CompositeCore::FPassInputArray& Inputs, const UE::CompositeCore::FPassContext& PassContext) const override
	{
		RDG_EVENT_SCOPE_STAT(GraphBuilder, FCompositeColorGrade, "Composite.ColorGrade");
		RDG_GPU_STAT_SCOPE(GraphBuilder, FCompositeColorGrade);

		check(ValidateInputs(Inputs));

		const UE::CompositeCore::FResourceMetadata& Metadata = Inputs[0].Metadata;
		const FScreenPassTexture& Input = Inputs[0].Texture;
		FScreenPassRenderTarget Output = Inputs.OverrideOutput;

		// If the override output is provided, it means that this is the last pass in post processing.
		if (!Output.IsValid())
		{
			Output = CreateOutputRenderTarget(GraphBuilder, InView, PassContext.OutputViewRect, Input.Texture->Desc, TEXT("ColorGradeCompositePass"));
		}

		FGlobalShaderMap* GlobalShaderMap = GetGlobalShaderMap(InView.GetFeatureLevel());
		FScreenPassTextureViewport Viewport = FScreenPassTextureViewport(Output);

		FCompositeColorGradeShader::FParameters* Parameters = GraphBuilder.AllocParameters<FCompositeColorGradeShader::FParameters>();
		Parameters->InputTexture = Input.Texture;
		Parameters->InputSampler = TStaticSamplerState<SF_Bilinear>::GetRHI();
		Parameters->WorkingColorSpace = GDefaultWorkingColorSpaceUniformBuffer.GetUniformBufferRef();
		Parameters->Global = FCompositeColorGradeShader::ToParameters(ColorGradingSettings.Global);
		Parameters->Shadows = FCompositeColorGradeShader::ToParameters(ColorGradingSettings.Shadows);
		Parameters->Midtones = FCompositeColorGradeShader::ToParameters(ColorGradingSettings.Midtones);
		Parameters->Highlights = FCompositeColorGradeShader::ToParameters(ColorGradingSettings.Highlights);
		Parameters->ShadowsMax = ColorGradingSettings.ShadowsMax;
		Parameters->HighlightsMin = ColorGradingSettings.HighlightsMin;
		Parameters->HighlightsMax = ColorGradingSettings.HighlightsMax;
		Parameters->bIsTemperatureWhiteBalance = (TemperatureSettings.TemperatureType == ETemperatureMethod::TEMP_WhiteBalance);
		Parameters->WhiteTemp = TemperatureSettings.WhiteTemp;
		Parameters->WhiteTint = TemperatureSettings.WhiteTint;
		Parameters->RenderTargets[0] = Output.GetRenderTargetBinding();

		TShaderMapRef<FCompositeColorGradeShader> PixelShader(GlobalShaderMap);
		AddDrawScreenPass(
			GraphBuilder,
			RDG_EVENT_NAME("Composite.ColorGrade (%dx%d)", Viewport.Extent.X, Viewport.Extent.Y),
			InView,
			Viewport,
			Viewport,
			PixelShader,
			Parameters
		);

		return UE::CompositeCore::FPassTexture{ MoveTemp(Output), Metadata };
	}

	FCompositeTemperatureSettings TemperatureSettings;
	FColorGradingSettings ColorGradingSettings;
};

UCompositePassColorGrade::UCompositePassColorGrade(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

UCompositePassColorGrade::~UCompositePassColorGrade() = default;

bool UCompositePassColorGrade::GetProxy(const UE::CompositeCore::FPassInputDecl& InputDecl, FSceneRenderingBulkObjectAllocator& InFrameAllocator, FCompositeCorePassProxy*& OutProxy) const
{
	FColorGradeCompositePassProxy* Proxy = InFrameAllocator.Create<FColorGradeCompositePassProxy>(UE::CompositeCore::FPassInputDeclArray{ InputDecl });
	Proxy->TemperatureSettings = TemperatureSettings;
	Proxy->ColorGradingSettings = ColorGradingSettings;

	OutProxy = Proxy;
	return true;
}


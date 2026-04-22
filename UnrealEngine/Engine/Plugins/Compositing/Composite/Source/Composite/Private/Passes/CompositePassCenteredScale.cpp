// Copyright Epic Games, Inc. All Rights Reserved.

#include "Passes/CompositePassCenteredScale.h"

#include "Camera/CameraComponent.h"
#include "CompositeActor.h"
#include "Engine/Texture2D.h"
#include "Layers/CompositeLayerPlate.h"
#include "MediaTexture.h"
#include "Passes/CompositeCorePassProxy.h"

DECLARE_GPU_STAT_NAMED(FCompositeCenteredScale, TEXT("Composite.CenteredScale"));

class FCompositeCenteredScaleShader : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FCompositeCenteredScaleShader);
	SHADER_USE_PARAMETER_STRUCT(FCompositeCenteredScaleShader, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, InputTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, InputSampler)
		SHADER_PARAMETER(FVector2f, ScaleUV)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FCompositeCenteredScaleShader, "/Plugin/Composite/Private/CompositeCenteredScale.usf", "MainPS", SF_Pixel);


namespace UE
{
	namespace CompositeCore
	{
		class FCenteredScalePassProxy : public FCompositeCorePassProxy
		{
		public:
			IMPLEMENT_COMPOSITE_PASS(FCenteredScalePassProxy);

			using FCompositeCorePassProxy::FCompositeCorePassProxy;

			FPassTexture Add(FRDGBuilder& GraphBuilder, const FSceneView& InView, const FPassInputArray& Inputs, const FPassContext& PassContext) const override
			{
				RDG_EVENT_SCOPE_STAT(GraphBuilder, FCompositeCenteredScale, "Composite.CenteredScale");
				RDG_GPU_STAT_SCOPE(GraphBuilder, FCompositeCenteredScale);

				check(ValidateInputs(Inputs));

				const UE::CompositeCore::FResourceMetadata& Metadata = Inputs[0].Metadata;
				const FScreenPassTexture& Input = Inputs[0].Texture;
				FScreenPassRenderTarget Output = Inputs.OverrideOutput;

				// If the override output is provided, it means that this is the last pass in post processing.
				if (!Output.IsValid())
				{
					Output = CreateOutputRenderTarget(GraphBuilder, InView, PassContext.OutputViewRect, Input.Texture->Desc, TEXT("CenteredScaleCompositePass"));
				}

				FGlobalShaderMap* GlobalShaderMap = GetGlobalShaderMap(InView.GetFeatureLevel());
				FScreenPassTextureViewport Viewport = FScreenPassTextureViewport(Output);

				FCompositeCenteredScaleShader::FParameters* Parameters = GraphBuilder.AllocParameters<FCompositeCenteredScaleShader::FParameters>();
				Parameters->InputTexture = Input.Texture;
				Parameters->InputSampler = TStaticSamplerState<SF_Bilinear>::GetRHI();
				Parameters->ScaleUV = ScaleUV;
				Parameters->RenderTargets[0] = Output.GetRenderTargetBinding();

				TShaderMapRef<FCompositeCenteredScaleShader> PixelShader(GlobalShaderMap);
				AddDrawScreenPass(
					GraphBuilder,
					RDG_EVENT_NAME("Composite.CenteredScale (%dx%d)", Viewport.Extent.X, Viewport.Extent.Y),
					InView,
					Viewport,
					Viewport,
					PixelShader,
					Parameters
				);

				return UE::CompositeCore::FPassTexture{ MoveTemp(Output), Metadata };
			}

			FVector2f ScaleUV = FVector2f::One();
		};
	}
}

UCompositePassCenteredScale::UCompositePassCenteredScale(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, ScaleMode(ECompositePassScaleMode::Automatic)
	, SourceAspectRatio(16.0f, 9.0f)
	, TargetAspectRatio(16.0f, 9.0f)
	, ScaleFactor(1.0f, 1.0f)
	, OverscanUncropMode(ECompositePassOverscanUncropMode::Automatic)
	, Overscan(0.0f)
{
}

UCompositePassCenteredScale::~UCompositePassCenteredScale() = default;

bool UCompositePassCenteredScale::GetProxy(const UE::CompositeCore::FPassInputDecl& InputDecl, FSceneRenderingBulkObjectAllocator& InFrameAllocator, FCompositeCorePassProxy*& OutProxy) const
{
	using namespace UE::CompositeCore;

	// Skip pass if there is no active scaling
	if (!IsActive())
	{
		return false;
	}

	FCenteredScalePassProxy* Proxy = InFrameAllocator.Create<FCenteredScalePassProxy>(FPassInputDeclArray{ InputDecl });
	Proxy->ScaleUV = CalculateScale();

	OutProxy = Proxy;
	return true;
}

bool UCompositePassCenteredScale::IsActive() const
{
	return IsEnabled() && !CalculateScale().Equals(FVector2f::One(), UE_SMALL_NUMBER);
}

FVector2f UCompositePassCenteredScale::CalculateScale() const
{
	// TODO: The scale UV could be cached, though special care would be needed to acocunt for dependent property changes

	ACompositeActor* CompositeActor = GetTypedOuter<ACompositeActor>();
	UCameraComponent* CameraComponent = nullptr;

	if (CompositeActor)
	{
		CameraComponent = Cast<UCameraComponent>(CompositeActor->GetCamera().GetComponent(nullptr));
	}

	FVector2f ScaleUV = FVector2f::One();

	auto ApplyAspectRatioScaleFn = [&ScaleUV](float SourceAR, float TargetAR)
		{
			if (SourceAR > TargetAR)
			{
				ScaleUV.X = (SourceAR != 0.0f) ? TargetAR / SourceAR : 0.0f;
			}
			else if (SourceAR < TargetAR)
			{
				ScaleUV.Y = (TargetAR != 0.0f) ? SourceAR / TargetAR : 0.0f;
			}
		};

	switch (ScaleMode)
	{
	case ECompositePassScaleMode::None:
		break;
	case ECompositePassScaleMode::Automatic:
	{
		float SourceAR = 16.0f / 9.0f;

		if (UCompositeLayerPlate* PlateLayer = GetTypedOuter<UCompositeLayerPlate>())
		{
			if (IsValid(PlateLayer->Texture))
			{
				if (const UMediaTexture* MediaTexture = Cast<UMediaTexture>(PlateLayer->Texture))
				{
					SourceAR = MediaTexture->CurrentAspectRatio;
				}
				else if (const UTexture2D* Texture2D = Cast<UTexture2D>(PlateLayer->Texture))
				{
					SourceAR = Texture2D->GetSizeX() / static_cast<float>(Texture2D->GetSizeY());
				}
			}
		}

		const float TargetAR = CameraComponent ? CameraComponent->AspectRatio : SourceAR;

		ApplyAspectRatioScaleFn(SourceAR, TargetAR);
	}
	break;
	case ECompositePassScaleMode::AspectRatio:
	{
		const float SourceAR = SourceAspectRatio.X / SourceAspectRatio.Y;
		const float TargetAR = TargetAspectRatio.X / TargetAspectRatio.Y;

		ApplyAspectRatioScaleFn(SourceAR, TargetAR);
	}
	break;
	case ECompositePassScaleMode::Manual:
		ScaleUV /= ScaleFactor;
		break;

	default:
		checkNoEntry();
		break;
	};

	switch (OverscanUncropMode)
	{
	case ECompositePassOverscanUncropMode::None:
		break;
	case ECompositePassOverscanUncropMode::Automatic:
		if (CameraComponent)
		{
			ScaleUV *= 1.0f + CameraComponent->Overscan;
		}
		break;
	case ECompositePassOverscanUncropMode::Manual:
		ScaleUV *= 1.0f + Overscan;
		break;

	default:
		checkNoEntry();
		break;
	}

	return ScaleUV;
}


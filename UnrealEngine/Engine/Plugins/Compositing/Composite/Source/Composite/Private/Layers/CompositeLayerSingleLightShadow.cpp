// Copyright Epic Games, Inc. All Rights Reserved.

#include "Layers/CompositeLayerSingleLightShadow.h"

#include "Camera/CameraComponent.h"
#include "CompositeActor.h"
#include "CompositeRenderTargetPool.h"
#include "Passes/CompositePassMaterial.h"
#include "Components/CompositeSceneCapture2DComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Passes/CompositeCorePassProxy.h"
#include "RHI.h"

DECLARE_GPU_STAT_NAMED(FCompositeSingleLightShadow, TEXT("Composite.SingleLayerShadow"));

class FCompositeSingleLightShadowShader : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FCompositeSingleLightShadowShader);
	SHADER_USE_PARAMETER_STRUCT(FCompositeSingleLightShadowShader, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneDepthTex)
		SHADER_PARAMETER_SAMPLER(SamplerState, SceneDepthSampler)

		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, ShadowDepthTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, ShadowDepthTextureSampler)

		SHADER_PARAMETER(FVector4f, SceneDepthPositionScaleBias)
		SHADER_PARAMETER(FMatrix44f, SceneDepthScreenToWorld)
		SHADER_PARAMETER(FMatrix44f, ShadowMatrix)
		SHADER_PARAMETER(FVector4f, ShadowBufferSize)
		SHADER_PARAMETER(FVector4f, ShadowInvDeviceZToWorldZ)
		SHADER_PARAMETER(FVector4f, ShadowStrengthBias)
		SHADER_PARAMETER(FVector3f, LightWorldDirection)
		SHADER_PARAMETER(uint32, bIsPerspectiveProjection)

		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FCompositeSingleLightShadowShader, "/Plugin/Composite/Private/CompositeSingleLightShadow.usf", "MainPS", SF_Pixel);


namespace UE
{
	namespace CompositeCore
	{
		/**
		* Utility function to create the inverse depth projection transform to be used by the shader system.
		* Copied from SceneView.cpp.
		*/
		FVector4f CreateInvDeviceZToWorldZTransform(const FMatrix& ProjMatrix)
		{
			float DepthMul = (float)ProjMatrix.M[2][2];
			float DepthAdd = (float)ProjMatrix.M[3][2];

			if (DepthAdd == 0.f)
			{
				// Avoid dividing by 0 in this case
				DepthAdd = 0.00000001f;
			}

			bool bIsPerspectiveProjection = ProjMatrix.M[3][3] < 1.0f;
			if (bIsPerspectiveProjection)
			{
				float SubtractValue = DepthMul / DepthAdd;

				// Subtract a tiny number to avoid divide by 0 errors in the shader when a very far distance is decided from the depth buffer.
				// This fixes fog not being applied to the black background in the editor.
				SubtractValue -= 0.00000001f;

				return FVector4f( 0.0f, 0.0f, 1.0f / DepthAdd, SubtractValue );
			}
			else
			{
				return FVector4f((float)(1.0f / ProjMatrix.M[2][2]), (float)(-ProjMatrix.M[3][2] / ProjMatrix.M[2][2] + 1.0f), 0.0f, 1.0f);
			}
		}

		class FCompositeSingleLightShadowProxy : public FCompositeCorePassProxy
		{
		public:
			IMPLEMENT_COMPOSITE_PASS(FCompositeSingleLightShadowProxy);

			using FCompositeCorePassProxy::FCompositeCorePassProxy;

			FPassTexture Add(FRDGBuilder& GraphBuilder, const FSceneView& InView, const FPassInputArray& Inputs, const FPassContext& PassContext) const override
			{
				RDG_EVENT_SCOPE_STAT(GraphBuilder, FCompositeSingleLightShadow, "Composite.SingleLayerShadow");
				RDG_GPU_STAT_SCOPE(GraphBuilder, FCompositeSingleLightShadow);

				check(ValidateInputs(Inputs));

				const UE::CompositeCore::FResourceMetadata& Metadata = Inputs[0].Metadata;
				const FScreenPassTexture& SceneDepth = Inputs[0].Texture;
				const FScreenPassTexture& ShadowMap = Inputs[1].Texture;
				FScreenPassRenderTarget Output = Inputs.OverrideOutput;

				if (!Output.IsValid())
				{
					FRDGTextureDesc Desc = SceneDepth.Texture->Desc;
					Desc.Format = FSceneTexturesConfig::Get().ColorFormat;
					Output = CreateOutputRenderTarget(GraphBuilder, InView, PassContext.OutputViewRect, Desc, TEXT("CompositeSingleLayerShadow"));
				}

				// Calculate scene depth position scale bias
				const FIntPoint Resolution = FIntPoint(SceneDepth.ViewRect.Size().X, SceneDepth.ViewRect.Size().Y);
				FVector4f SceneDepthPositionScaleBias = FSceneView::GetScreenPositionScaleBias(Resolution, FIntRect(FIntPoint::ZeroValue, Resolution));

				// Calculate scene depth screen to world matrix (without jitter).
				FMatrix44f SceneDepthScreenToWorld = FMatrix44f(
					InView.ViewMatrices.GetScreenToClipMatrix() *
					InView.ViewMatrices.GetProjectionNoAAMatrix().Inverse() *
					InView.ViewMatrices.GetInvViewMatrix()
				);

				// The shadow map texture size
				const FIntVector ShadowMapSize = ShadowMap.Texture->Desc.GetSize();

				FGlobalShaderMap* GlobalShaderMap = GetGlobalShaderMap(InView.GetFeatureLevel());
				FScreenPassTextureViewport Viewport = FScreenPassTextureViewport(Output);

				FCompositeSingleLightShadowShader::FParameters* Parameters = GraphBuilder.AllocParameters<FCompositeSingleLightShadowShader::FParameters>();
				Parameters->SceneDepthTex				= SceneDepth.Texture;
				Parameters->SceneDepthSampler			= TStaticSamplerState<SF_Bilinear>::GetRHI();
				Parameters->ShadowDepthTexture			= ShadowMap.Texture;
				Parameters->ShadowDepthTextureSampler	= TStaticSamplerState<SF_Point>::GetRHI();
				Parameters->SceneDepthPositionScaleBias	= SceneDepthPositionScaleBias;
				Parameters->SceneDepthScreenToWorld		= SceneDepthScreenToWorld;
				Parameters->ShadowInvDeviceZToWorldZ	= ShadowInvDeviceZToWorldZ;
				Parameters->ShadowMatrix				= ShadowMatrix;
				Parameters->ShadowBufferSize			= FVector4f((float)ShadowMapSize.X, (float)ShadowMapSize.Y, 1.0f / (float)ShadowMapSize.X, 1.0f / (float)ShadowMapSize.Y);
				Parameters->ShadowStrengthBias		= ShadowStrengthBias;
				Parameters->LightWorldDirection			= LightWorldDirection;
				// We can use the current view since we the depth render matches the main view
				Parameters->bIsPerspectiveProjection	= InView.IsPerspectiveProjection();
				Parameters->RenderTargets[0]			= Output.GetRenderTargetBinding();

				TShaderMapRef<FCompositeSingleLightShadowShader> PixelShader(GlobalShaderMap);
				AddDrawScreenPass(
					GraphBuilder,
					RDG_EVENT_NAME("Composite.SingleLayerShadow (%dx%d)", Viewport.Extent.X, Viewport.Extent.Y),
					InView,
					Viewport,
					Viewport,
					PixelShader,
					Parameters
				);

				return UE::CompositeCore::FPassTexture{ MoveTemp(Output), Metadata };
			}

			FMatrix44f ShadowMatrix;
			FVector4f ShadowInvDeviceZToWorldZ;
			FVector4f ShadowStrengthBias;
			FVector3f LightWorldDirection;
		};
	}
}

UCompositeLayerSingleLightShadow::UCompositeLayerSingleLightShadow(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	Operation = ECompositeCoreMergeOp::Multiply;

	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		// 5.7 hotfix: We must use delegates since adding virtual function overrides is not permitted
		FWorldDelegates::OnWorldPreSendAllEndOfFrameUpdates.AddUObject(this, &UCompositeLayerSingleLightShadow::OnEndOfFrameUpdate);
	}
}

void UCompositeLayerSingleLightShadow::OnRemoved(const UWorld* World)
{
	ACompositeActor* CompositeActor = GetTypedOuter<ACompositeActor>();
	// Note: We don't use IsValid(..) since destruction should proceed even if the actor is pending kill.
	if (CompositeActor != nullptr)
	{
		CompositeActor->DestroySceneCaptures(this);
	}
}

void UCompositeLayerSingleLightShadow::BeginDestroy()
{
	Super::BeginDestroy();

	OnRemoved(GetWorld()); // Redundant remove call for safety

	FWorldDelegates::OnWorldPreSendAllEndOfFrameUpdates.RemoveAll(this);
}

#if WITH_EDITOR
void UCompositeLayerSingleLightShadow::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName PropertyName = PropertyChangedEvent.GetMemberPropertyName();

	if (PropertyName == GET_MEMBER_NAME_CHECKED(ThisClass, ShadowCastingActors))
	{
		SetShadowCastingActors(MoveTemp(ShadowCastingActors));
	}
}

void UCompositeLayerSingleLightShadow::PostEditUndo()
{
	Super::PostEditUndo();

	SetShadowCastingActors(MoveTemp(ShadowCastingActors));
}
#endif

bool UCompositeLayerSingleLightShadow::GetProxy(FTraversalContext& InContext, FSceneRenderingBulkObjectAllocator& InFrameAllocator, FCompositeCorePassProxy*& OutProxy) const
{
	using namespace UE::CompositeCore;

	ACompositeActor* CompositeActor = GetTypedOuter<ACompositeActor>();
	if (!IsValid(CompositeActor) || !IsValid(Light))
	{
		return false;
	}

	// Disable with zero strength
	if (ShadowStrength <= 0.0f)
	{
		return false;
	}

	if (!CachedShadowMapCapture.IsValid())
	{
		CachedShadowMapCapture = FindOrCreateShadowMapCapture(*CompositeActor);
	}

	if (!CachedSceneDepthCapture.IsValid())
	{
		CachedSceneDepthCapture = FindOrCreateSceneDepthCapture(*CompositeActor);
	}

	// Both cached scene captures are now expected to be valid
	if (!CachedShadowMapCapture.IsValid() || !CachedSceneDepthCapture.IsValid())
	{
		return false;
	}

	// Update properties that may be dynamic
	{
		// Keep exposed properties & transform in sync
		CachedShadowMapCapture->OrthoWidth = OrthographicWidth;
		CachedShadowMapCapture->SetWorldTransform(Light->GetTransform());

		// Acquire render target
		FCompositeRenderTargetPool::Get().ConditionalAcquireTarget(CachedShadowMapCapture.Get(), CachedShadowMapCapture->TextureTarget, FIntPoint(ShadowMapResolution, ShadowMapResolution), RTF_R32f);
		FCompositeRenderTargetPool::Get().ConditionalAcquireTarget(CachedSceneDepthCapture.Get(), CachedSceneDepthCapture->TextureTarget, GetRenderResolution(), RTF_R32f);
	}
	
	// Setup child shadow pass proxy
	FCompositeSingleLightShadowProxy* ShadowProxy;
	{
		// Display-resolution scene depth without jitter as an external texture.
		const ResourceId SceneDepthId = InContext.FindOrCreateExternalTexture(CachedSceneDepthCapture->TextureTarget, {});

		// Shadow map texture
		const ResourceId ShadowMapId = InContext.FindOrCreateExternalTexture(CachedShadowMapCapture->TextureTarget, {});

		// Setup shadow pass inputs
		FPassInputDeclArray PassInputs;
		PassInputs.SetNum(2);
		PassInputs[0].Set<FPassExternalResourceDesc>({ SceneDepthId });
		PassInputs[1].Set<FPassExternalResourceDesc>({ ShadowMapId });

		ShadowProxy = InFrameAllocator.Create<FCompositeSingleLightShadowProxy>(PassInputs);
		ShadowProxy->ShadowStrengthBias = FVector4f(ShadowStrength, 0.0f, 0.0f, 0.0f);
		GetShadowMatrices(*CachedShadowMapCapture, ShadowProxy->ShadowMatrix, ShadowProxy->ShadowInvDeviceZToWorldZ);
	}
	ShadowProxy->LightWorldDirection = FVector3f(Light->GetTransform().TransformVector(FVector(1, 0, 0)));

	FPassInputDeclArray Inputs;
	Inputs.SetNum(FixedNumLayerInputs);
	Inputs[0].Set<const FCompositeCorePassProxy*>(ShadowProxy);
	Inputs[1] = GetDefaultSecondInput(InContext);

	OutProxy = InFrameAllocator.Create<FMergePassProxy>(MoveTemp(Inputs), GetMergeOperation(InContext), TEXT("SingleLightShadowCatcher"));
	return true;
}

const TArray<TSoftObjectPtr<AActor>> UCompositeLayerSingleLightShadow::GetShadowCastingActors() const
{
	return ShadowCastingActors;
}

void UCompositeLayerSingleLightShadow::SetShadowCastingActors(TArray<TSoftObjectPtr<AActor>> InShadowCaster)
{
	ShadowCastingActors = InShadowCaster;

	ACompositeActor* CompositeActor = GetTypedOuter<ACompositeActor>();
	if (!IsValid(CompositeActor))
	{
		return;
	}

	if (!CachedShadowMapCapture.IsValid())
	{
		CachedShadowMapCapture = FindOrCreateShadowMapCapture(*CompositeActor);
	}

	if (!CachedSceneDepthCapture.IsValid())
	{
		CachedSceneDepthCapture = FindOrCreateSceneDepthCapture(*CompositeActor);
	}

	if (CachedShadowMapCapture.IsValid())
	{
		UpdateSceneCaptureComponents(*CachedShadowMapCapture, ShadowCastingActors);
	}
	
	if (CachedSceneDepthCapture.IsValid())
	{
		// First, we empty hidden actors since our previous logic used them
		CachedSceneDepthCapture->HiddenActors.Empty();

		UpdateSceneCaptureComponents(*CachedSceneDepthCapture, ShadowCastingActors);
	}
}

UCompositeSceneCapture2DComponent* UCompositeLayerSingleLightShadow::FindOrCreateSceneDepthCapture(ACompositeActor& InOuter) const
{
	UCompositeSceneCapture2DComponent* SceneDepthCapture = InOuter.FindOrCreateSceneCapture<UCompositeSceneCapture2DComponent>(this, 1, FName("SceneDepthCapture"));
	
	if (ensure(IsValid(SceneDepthCapture)))
	{
		SceneDepthCapture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_RenderScenePrimitives;
		SceneDepthCapture->CaptureSource = SCS_SceneDepth;
		SceneDepthCapture->bRenderInMainRenderer = true;
		SceneDepthCapture->bIgnoreScreenPercentage = true;
	}

	return SceneDepthCapture;
}

UCompositeSceneCapture2DComponent* UCompositeLayerSingleLightShadow::FindOrCreateShadowMapCapture(ACompositeActor& InOuter) const
{
	UCompositeSceneCapture2DComponent* ShadowMapCapture = InOuter.FindOrCreateSceneCapture<UCompositeSceneCapture2DComponent>(this, 0, FName("ShadowMapCapture"));

	if (ensure(IsValid(ShadowMapCapture)))
	{
		ShadowMapCapture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
		ShadowMapCapture->CaptureSource = SCS_SceneDepth;
		ShadowMapCapture->bRenderInMainRenderer = true;
		ShadowMapCapture->bMainViewFamily = false;
		ShadowMapCapture->bMainViewResolution = false;
		ShadowMapCapture->bMainViewCamera = false;
		ShadowMapCapture->bInheritMainViewCameraPostProcessSettings = false;
		ShadowMapCapture->bUseRayTracingIfEnabled = false;
		ShadowMapCapture->ProjectionType = ECameraProjectionMode::Orthographic;
	}

	return ShadowMapCapture;
}

void UCompositeLayerSingleLightShadow::UpdateSceneCaptureComponents(USceneCaptureComponent2D& SceneCaptureComponent, TArrayView<TSoftObjectPtr<AActor>> InActors) const
{
	auto UpdateComponentsFn = [InActors](TArray<TWeakObjectPtr<UPrimitiveComponent>>& InComponents) -> void
		{
			InComponents.Reset(InActors.Num());
			
			for (const TSoftObjectPtr<AActor>& SoftActor : InActors)
			{
				if (const AActor* Actor = SoftActor.Get())
				{
					for (UActorComponent* Component : Actor->GetComponents())
					{
						if (UPrimitiveComponent* PrimitiveComponent = Cast<UPrimitiveComponent>(Component))
						{
							InComponents.Add(PrimitiveComponent);
						}
					}
				}
			}
		};

	if (SceneCaptureComponent.PrimitiveRenderMode == ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList)
	{
		UpdateComponentsFn(SceneCaptureComponent.ShowOnlyComponents);
	}
	else
	{
		UpdateComponentsFn(SceneCaptureComponent.HiddenComponents);
	}
}

void UCompositeLayerSingleLightShadow::OnEndOfFrameUpdate(UWorld* InWorld)
{
	/**
	* In order to retain the ability to reference actors in sublevels, we prefer using weak pointer components on scene captures. However,
	* primitive components may often be recreated (see RerunConstructionScripts(), FStaticMeshComponentRecreateRenderStateContext, dynamic
	* text primitives, etc). To prevent issues with destroyed primitives disappearing in our scene captures, we re-register them every frame.
	*/
	if (IsEnabled())
	{
		ACompositeActor* CompositeActor = GetTypedOuter<ACompositeActor>();

		if (IsValid(CompositeActor) && CompositeActor->GetCompositeLayers().Contains(this))
		{
			if (CachedShadowMapCapture.IsValid())
			{
				UpdateSceneCaptureComponents(*CachedShadowMapCapture, ShadowCastingActors);
			}

			if (CachedSceneDepthCapture.IsValid())
			{
				UpdateSceneCaptureComponents(*CachedSceneDepthCapture, ShadowCastingActors);
			}
		}
	}
}

void UCompositeLayerSingleLightShadow::GetShadowMatrices(UCompositeSceneCapture2DComponent& ShadowMapCapture, FMatrix44f& OutShadowMatrix, FVector4f& OutShadowInvDeviceZToWorldZ) const
{
	FMinimalViewInfo ViewInfo;
	ShadowMapCapture.GetCameraView(GetWorld()->DeltaTimeSeconds, ViewInfo);

	// Ortho clip plane values from SceneCaptureRendering.cpp
	ViewInfo.OrthoNearClipPlane = 0;
	ViewInfo.OrthoFarClipPlane = UE_FLOAT_HUGE_DISTANCE / 4.0f;

	FMatrix ViewRotationMatrix = FInverseRotationMatrix(ViewInfo.Rotation) * FMatrix(
		FPlane(0, 0, 1, 0),
		FPlane(1, 0, 0, 0),
		FPlane(0, 1, 0, 0),
		FPlane(0, 0, 0, 1));

	FVector LocalViewOrigin = ViewInfo.Location;
	if (!ViewRotationMatrix.GetOrigin().IsNearlyZero(0.0f))
	{
		LocalViewOrigin += ViewRotationMatrix.InverseTransformPosition(FVector::ZeroVector);
		ViewRotationMatrix = ViewRotationMatrix.RemoveTranslation();
	}
	const FMatrix ShadowViewMatrix = FTranslationMatrix(-LocalViewOrigin) * ViewRotationMatrix;
	const FMatrix ShadowProjectionMatrix = AdjustProjectionMatrixForRHI(ViewInfo.CalculateProjectionMatrix());
	
	//TODO: Max subject depth logic will need to be updated once support perspective projection.
	float MaxSubjectDepth = 1.0f;
	float InvMaxSubjectDepth = 1.0f / MaxSubjectDepth;
	const float ShadowResolutionFractionX = 0.5f;
	const float ShadowResolutionFractionY = 0.5f;

	// Note: See FProjectedShadowInfo::GetWorldToShadowMatrix as a reference.
	const FMatrix WorldToShadowMatrix = ShadowViewMatrix * ShadowProjectionMatrix *
		FMatrix(
			FPlane(ShadowResolutionFractionX, 0, 0, 0),
			FPlane(0, -ShadowResolutionFractionY, 0, 0),
			FPlane(0, 0, InvMaxSubjectDepth, 0),
			FPlane(ShadowResolutionFractionX, ShadowResolutionFractionY, 0, 1)
		);

	OutShadowMatrix = FMatrix44f(WorldToShadowMatrix);
	OutShadowInvDeviceZToWorldZ = UE::CompositeCore::CreateInvDeviceZToWorldZTransform(ShadowProjectionMatrix);
}



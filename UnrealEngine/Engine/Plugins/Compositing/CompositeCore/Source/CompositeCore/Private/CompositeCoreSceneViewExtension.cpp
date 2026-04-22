// Copyright Epic Games, Inc. All Rights Reserved.

#include "CompositeCoreSceneViewExtension.h"

#include "CompositeCoreModule.h"
#include "Passes/CompositeCorePassDilate.h"
#include "Passes/CompositeCorePassFXAAProxy.h"
#include "Passes/CompositeCorePassMergeProxy.h"

#include "CommonRenderResources.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Containers/Set.h"
#include "EngineUtils.h"
#include "Engine/Texture.h"
#include "HDRHelper.h"
#include "PostProcess/PostProcessAA.h"
#include "PostProcess/PostProcessMaterialInputs.h"
#include "Rendering/CustomRenderPass.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "SceneRenderTargetParameters.h"
#include "SceneView.h"
#include "ScreenPass.h"
#include "SystemTextures.h"
#include "TextureResource.h"

static TAutoConsoleVariable<int32> CVarCompositeCoreApplyFXAA(
	TEXT("CompositeCore.ApplyFXAA"),
	0,
	TEXT("When enabled, the custom render pass automatically applies FXAA."),
	ECVF_RenderThreadSafe);

/** Parameters used to transition a texture to SRVGraphics. */
BEGIN_SHADER_PARAMETER_STRUCT(FCompositeRenderTargetTransitionParameters, )
	RDG_TEXTURE_ACCESS(TransitionTexture, ERHIAccess::SRVGraphics)
END_SHADER_PARAMETER_STRUCT()

class FCompositeCoreCustomRenderPass : public FCustomRenderPassBase
{
public:
	IMPLEMENT_CUSTOM_RENDER_PASS(FCompositeCoreCustomRenderPass);

	FCompositeCoreCustomRenderPass(const FIntPoint& InRenderTargetSize, FCompositeCoreSceneViewExtension* InParentExtension, const FSceneView& InView, const UE::CompositeCore::FBuiltInRenderPassOptions& InOptions)
		: FCustomRenderPassBase(TEXT("CompositeCoreCustomRenderPass"), ERenderMode::DepthAndBasePass, ERenderOutput::SceneColorAndAlpha, InRenderTargetSize)
		, ParentExtension(InParentExtension)
		, ViewId(InView.GetViewKey())
		, ViewFeatureLevel(InView.GetFeatureLevel())
		, Inputs({ InOptions.DilationSize, InOptions.bOpacifyOutput })
	{
		bSceneColorWithTranslucent = true;
	}

	virtual void OnPreRender(FRDGBuilder& GraphBuilder) override
	{
		const FRDGTextureDesc TextureDesc = FRDGTextureDesc::Create2D(RenderTargetSize, PF_FloatRGBA, FClearValueBinding::Black, TexCreate_RenderTargetable | TexCreate_ShaderResource);
		RenderTargetTexture = GraphBuilder.CreateTexture(TextureDesc, TEXT("CompositeCoreCustomTexture"));
		AddClearRenderTargetPass(GraphBuilder, RenderTargetTexture, FLinearColor::Black, FIntRect(FInt32Point(), RenderTargetSize));
	}

	virtual void OnPostRender(FRDGBuilder& GraphBuilder) override
	{
		const FRDGTextureDesc TextureDesc = FRDGTextureDesc::Create2D(RenderTargetTexture->Desc.Extent, PF_FloatRGBA, FClearValueBinding::Black, TexCreate_UAV | TexCreate_ShaderResource | TexCreate_RenderTargetable);
		FRDGTextureRef Output = GraphBuilder.CreateTexture(TextureDesc, TEXT("CompositeCoreProcessedTexture"));

		UE::CompositeCore::Private::AddDilatePass(GraphBuilder, RenderTargetTexture, Output, ViewFeatureLevel, Inputs);

		ParentExtension->CollectCustomRenderTarget(ViewId, GraphBuilder.ConvertToExternalTexture(Output));
	}

private:

	FCompositeCoreSceneViewExtension* ParentExtension;
	const uint32 ViewId;
	const ERHIFeatureLevel::Type ViewFeatureLevel;
	const UE::CompositeCore::Private::FDilateInputs Inputs;
};

FCompositeCoreSceneViewExtension::FCompositeCoreSceneViewExtension(const FAutoRegister& AutoReg, UWorld* InWorld)
	: FWorldSceneViewExtension(AutoReg, InWorld)
{
}

FCompositeCoreSceneViewExtension::~FCompositeCoreSceneViewExtension() = default;

void FCompositeCoreSceneViewExtension::RegisterPrimitives_GameThread(const TArray<UPrimitiveComponent*>& InPrimitiveComponents)
{
	check(IsInGameThread());

	for (UPrimitiveComponent* InPrimitiveComponent : InPrimitiveComponents)
	{
		if (!IsValid(InPrimitiveComponent))
		{
			continue;
		}

		if (!CompositePrimitives.Contains(InPrimitiveComponent))
		{
			CompositePrimitives.Add(InPrimitiveComponent);
		}

		if (!InPrimitiveComponent->bHoldout)
		{
			InPrimitiveComponent->Modify();
			InPrimitiveComponent->SetHoldout(true);
		}
	}
}

void FCompositeCoreSceneViewExtension::UnregisterPrimitives_GameThread(const TArray<UPrimitiveComponent*>& InPrimitiveComponents)
{
	check(IsInGameThread());

	for (UPrimitiveComponent* InPrimitiveComponent : InPrimitiveComponents)
	{
		if (!IsValid(InPrimitiveComponent))
		{
			continue;
		}

		if (CompositePrimitives.Contains(InPrimitiveComponent))
		{
			CompositePrimitives.Remove(InPrimitiveComponent);
		}

		if (InPrimitiveComponent->bHoldout)
		{
			InPrimitiveComponent->Modify();
			InPrimitiveComponent->SetHoldout(false);
		}
	}
}

void FCompositeCoreSceneViewExtension::SetRenderWork_GameThread(UE::CompositeCore::FRenderWork&& InWork)
{
	bHasCustomRenderWork = !InWork.IsEmpty();
	MainRenderMode = MoveTemp(InWork.MainRenderMode);
	// Copy since view modes are used on both game and render threads (see IsActiveForViewFamily)
	AllowedViewModes = InWork.AllowedViewModes;
	SceneCapturesUpdateQueue = MoveTemp(InWork.SceneCapturesUpdateQueue);

	ENQUEUE_RENDER_COMMAND(CopyCompositeCoreRenderWork)(
		[FrameWork = MoveTemp(InWork), WeakThis = AsWeak()](FRHICommandListImmediate& RHICmdList) mutable
		{
			TSharedPtr<FCompositeCoreSceneViewExtension> SVE = StaticCastSharedPtr<FCompositeCoreSceneViewExtension>(WeakThis.Pin());
			if (SVE.IsValid())
			{
				SVE->ExternalInputs_RenderThread.Reset();

				for (int32 Index=0; Index < FrameWork.ExternalInputs.Num(); ++Index)
				{
					const UE::CompositeCore::FExternalTexture& ExternalInput = FrameWork.ExternalInputs[Index];
					TStrongObjectPtr<UTexture> ExternalTexture = ExternalInput.Texture.Pin();
					
					if (ExternalTexture.IsValid())
					{
						if (FTextureResource* TexResource = ExternalTexture->GetResource())
						{
							const int32 MapIndex = UE::CompositeCore::EXTERNAL_RANGE_START_ID + Index;

							UE::CompositeCore::FExternalRenderTarget& Target = SVE->ExternalInputs_RenderThread.Add(MapIndex);
							Target.RenderTarget = CreateRenderTarget(TexResource->GetTextureRHI(), TEXT("CompositeExternalInput"));
							Target.Metadata = ExternalInput.Metadata;
						}
					}
				}

				SVE->RenderWork_RenderThread = MoveTemp(FrameWork);
			}
		});
}

void FCompositeCoreSceneViewExtension::ResetRenderWork_GameThread()
{
	bHasCustomRenderWork = false;
	MainRenderMode.Reset();
	AllowedViewModes.Reset();
	SceneCapturesUpdateQueue.Reset();

	ENQUEUE_RENDER_COMMAND(CopyCompositeCoreRenderWork)(
		[WeakThis = AsWeak()](FRHICommandListImmediate& RHICmdList)
		{
			TSharedPtr<FCompositeCoreSceneViewExtension> SVE = StaticCastSharedPtr<FCompositeCoreSceneViewExtension>(WeakThis.Pin());
			if (SVE.IsValid())
			{
				SVE->RenderWork_RenderThread.Reset();
				SVE->ExternalInputs_RenderThread.Reset();
			}
		});
}

void FCompositeCoreSceneViewExtension::SetBuiltInRenderPassOptions_GameThread(const UE::CompositeCore::FBuiltInRenderPassOptions& InOptions)
{
	BuiltInRenderPassOptions = InOptions;
}

void FCompositeCoreSceneViewExtension::ResetBuiltInRenderPassOptions_GameThread()
{
	BuiltInRenderPassOptions.Reset();
}

/* Called by the custom render pass to store its view render target for this frame. */

void FCompositeCoreSceneViewExtension::CollectCustomRenderTarget(uint32 InViewId, const TRefCountPtr<IPooledRenderTarget>& InRenderTarget)
{
	CustomRenderTargetPerView_RenderThread.Add(InViewId, InRenderTarget);
}

bool FCompositeCoreSceneViewExtension::IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const
{
	bool bIsActive = FWorldSceneViewExtension::IsActiveThisFrame_Internal(Context);
	bIsActive &= bHasCustomRenderWork || !CompositePrimitives.IsEmpty();
	bIsActive &= !IsHDREnabled();

	return bIsActive;
}


//~ Begin ISceneViewExtension Interface

int32 FCompositeCoreSceneViewExtension::GetPriority() const
{
	return GetDefault<UCompositeCorePluginSettings>()->SceneViewExtensionPriority;
}

const UE::CompositeCore::FRenderWork& FCompositeCoreSceneViewExtension::GetRenderWork() const
{
	if (RenderWork_RenderThread.IsSet())
	{
		return RenderWork_RenderThread.GetValue();
	}
	else
	{
		return UE::CompositeCore::FRenderWork::GetDefault();
	}
}

void FCompositeCoreSceneViewExtension::SetupViewFamily(FSceneViewFamily& InViewFamily)
{
	// Cleanup invalid primitives.
	for (auto Iter = CompositePrimitives.CreateIterator(); Iter; ++Iter)
	{
		if (!Iter->IsValid())
		{
			Iter.RemoveCurrent();
		}
	}
}

void FCompositeCoreSceneViewExtension::SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView)
{
	const bool bIsFirstView = (&InView == InViewFamily.Views[0]);
	if (bIsFirstView)
	{
		// The family view mode is only accessible after SetupViewFamily is called currently.
		if (!AllowedViewModes.IsEmpty() && !AllowedViewModes.Contains(InViewFamily.ViewMode))
		{
			// Optionally disable primitive alpha holdout outside of allowed view modes
			InViewFamily.EngineShowFlags.SetAllowPrimitiveAlphaHoldout(false);
		}
		else if(!InView.bIsSceneCapture)
		{
			// Manually update scene captures that have been registered but aren't already updating every frame
			for (TWeakObjectPtr<USceneCaptureComponent2D>& SceneCapture : SceneCapturesUpdateQueue)
			{
				if (SceneCapture.IsValid() && !SceneCapture->bCaptureEveryFrame)
				{
					SceneCapture->CaptureSceneDeferred();
				}
			}

			// Empty update queue
			SceneCapturesUpdateQueue.Reset();
		}
	}
}

void FCompositeCoreSceneViewExtension::BeginRenderViewFamily(FSceneViewFamily& InViewFamily)
{
	if (!IsActiveForViewFamily(InViewFamily))
	{
		return;
	}

	if (MainRenderMode.IsSet())
	{
		InViewFamily.SceneCaptureSource = MainRenderMode.GetValue();
	}

	const TWeakObjectPtr<UWorld> WorldPtr = GetWorld();
	check(WorldPtr.IsValid());

	for (int32 ViewIndex = 0; ViewIndex < InViewFamily.Views.Num(); ++ViewIndex)
	{
		const FSceneView& InView = *InViewFamily.Views[ViewIndex];

		TSet<FPrimitiveComponentId> CompositeCorePrimitiveIds;
		for (const TWeakObjectPtr<UPrimitiveComponent>& PrimitivePtr : CompositePrimitives)
		{
			TStrongObjectPtr<UPrimitiveComponent> Primitive = PrimitivePtr.Pin();
			// Collect only those primitives that use the bHoldout flag
			// The user can directly change this flag outside of this VE.
			if (Primitive.IsValid() && Primitive->bHoldout)
			{
				const FPrimitiveComponentId PrimId = Primitive->GetPrimitiveSceneId();

				if (InView.ShowOnlyPrimitives.IsSet())
				{
					if (InView.ShowOnlyPrimitives.GetValue().Contains(PrimId))
					{
						CompositeCorePrimitiveIds.Add(Primitive->GetPrimitiveSceneId());
					}
				}
				else if (!InView.HiddenPrimitives.Contains(PrimId))
				{
					CompositeCorePrimitiveIds.Add(Primitive->GetPrimitiveSceneId());
				}
			}
		}

		if (CompositeCorePrimitiveIds.IsEmpty())
		{
			continue;
		}

		UE::CompositeCore::FBuiltInRenderPassOptions RenderPassOptions = BuiltInRenderPassOptions.IsSet() ? *BuiltInRenderPassOptions : UE::CompositeCore::FBuiltInRenderPassOptions{};

		// Create a new custom render pass to render the composite primitive(s)
		FCompositeCoreCustomRenderPass* CustomRenderPass = new FCompositeCoreCustomRenderPass(
			InView.UnscaledViewRect.Size(),
			this,
			InView,
			RenderPassOptions
		);

		FSceneInterface::FCustomRenderPassRendererInput PassInput{};
		PassInput.EngineShowFlags = InViewFamily.EngineShowFlags;
		PassInput.EngineShowFlags.DisableFeaturesForUnlit();
		PassInput.EngineShowFlags.SetTranslucency(true);
		PassInput.EngineShowFlags.SetUnlitViewmode(RenderPassOptions.bEnableUnlitViewmode);
		PassInput.EngineShowFlags.SetAllowPrimitiveAlphaHoldout(false);
		if (RenderPassOptions.ViewUserFlagsOverride.IsSet())
		{
			PassInput.bOverridesPostVolumeUserFlags = true;
			PassInput.PostVolumeUserFlags = RenderPassOptions.ViewUserFlagsOverride.GetValue();
		}
		// Note: Incoming view location is invalid for scene captures
		PassInput.ViewLocation = InView.ViewMatrices.GetViewOrigin();
		PassInput.ViewRotationMatrix = InView.ViewMatrices.GetViewMatrix().RemoveTranslation();
		PassInput.ViewRotationMatrix.RemoveScaling();

		// Note: Projection matrix here is without jitter, GetProjectionNoAAMatrix() is invalid (not yet available).
		PassInput.ProjectionMatrix = InView.ViewMatrices.GetProjectionMatrix();
		PassInput.ViewActor = InView.ViewActor;
		PassInput.ShowOnlyPrimitives = CompositeCorePrimitiveIds;
		PassInput.CustomRenderPass = CustomRenderPass;
		PassInput.bIsSceneCapture = true;

		WorldPtr.Get()->Scene->AddCustomRenderPass(&InViewFamily, PassInput);
	}
}

void FCompositeCoreSceneViewExtension::PreRenderViewFamily_RenderThread(FRDGBuilder& GraphBuilder, FSceneViewFamily& InViewFamily)
{
	// To prevent cache reallocations each frame, we keep the existing views unless they start exceeding a reasonable number.
	constexpr int32 MaxNumCachedViews = 16;

	if (CachedOutputsPerView_RenderThread.Num() <= MaxNumCachedViews)
	{
		for (auto& Pair : CachedOutputsPerView_RenderThread)
		{
			// Reset each pre-cached view's output
			Pair.Value.Reset();
		}
	}
	else
	{
		CachedOutputsPerView_RenderThread.Reset();
	}
}

void FCompositeCoreSceneViewExtension::PreRenderView_RenderThread(FRDGBuilder& GraphBuilder, FSceneView& InView)
{
	static uint64 LastFrameCounter = 0;

	// We only apply pre-processing once per frame, on the very first render.
	if (LastFrameCounter != GFrameCounterRenderThread)
	{
		ApplyPreprocessing(GraphBuilder, InView);
		
		LastFrameCounter = GFrameCounterRenderThread;
	}
}

void FCompositeCoreSceneViewExtension::PrePostProcessPass_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& InView, const FPostProcessingInputs& Inputs)
{
	using namespace UE::CompositeCore;

	TRefCountPtr<IPooledRenderTarget>* CompositeRenderPassPtr = CustomRenderTargetPerView_RenderThread.Find(InView.GetViewKey());

	if ((CompositeRenderPassPtr != nullptr) && CVarCompositeCoreApplyFXAA.GetValueOnRenderThread())
	{
		static FFXAAPassProxy FXAAPassProxy = FFXAAPassProxy(GetDefaultInputDeclArray());

		// Set the composite render target as the pass input
		FPassInputArray PassInputs;
		FPassInput& PassInput = PassInputs.GetArray().AddDefaulted_GetRef();
		PassInput.Texture = FScreenPassTexture{ GraphBuilder.RegisterExternalTexture(*CompositeRenderPassPtr) };

		// Apply FXAA, with additional fwd/inv display transform passes
		const FPassTexture Output = FXAAPassProxy.Add(GraphBuilder, InView, PassInputs, {});
		const FScreenPassTexture& ScreenTexOutput = Output.Texture;

		// Extract the result back into the composite render target
		*CompositeRenderPassPtr = GraphBuilder.ConvertToExternalTexture(ScreenTexOutput.Texture);
	}
}

void FCompositeCoreSceneViewExtension::SubscribeToPostProcessingPass(EPostProcessingPass PassId, const FSceneView& InView, FAfterPassCallbackDelegateArray& InOutPassCallbacks, bool bIsPassEnabled)
{
	if (!IsActiveForView(InView))
	{
		return;
	}

	if (GetRenderWork().FramePasses.Contains(PassId))
	{
		InOutPassCallbacks.Add(FAfterPassCallbackDelegate::CreateRaw(this, &FCompositeCoreSceneViewExtension::PostProcessWork_RenderThread, PassId));
	}
}

bool FCompositeCoreSceneViewExtension::IsActiveForViewFamily(const FSceneViewFamily& InViewFamily) const
{
	auto IsViewModeAllowedFn = [&InViewFamily](const TSet<EViewModeIndex>& InAllowedViewModes) -> bool
		{
			// If the allowed view modes set is empty, we allow all types.
			return InAllowedViewModes.IsEmpty() ? true : InAllowedViewModes.Contains(InViewFamily.ViewMode);
		};

	bool bIsActive = true;
	bIsActive &= static_cast<bool>(InViewFamily.EngineShowFlags.AllowPrimitiveAlphaHoldout);

	if (IsInRenderingThread())
	{
		if (RenderWork_RenderThread.IsSet())
		{
			bIsActive &= IsViewModeAllowedFn(RenderWork_RenderThread.GetValue().AllowedViewModes);
		}
	}
	else if(IsInGameThread())
	{
		bIsActive &= IsViewModeAllowedFn(AllowedViewModes);
	}

	return bIsActive;
}

bool FCompositeCoreSceneViewExtension::IsActiveForView(const FSceneView& InView) const
{
	return IsActiveForViewFamily(*InView.Family);
}

void FCompositeCoreSceneViewExtension::ApplyPreprocessing(FRDGBuilder& GraphBuilder, FSceneView& InView) const
{
	using namespace UE::CompositeCore;

	const TSortedMap<ResourceId, TArray<const FCompositeCorePassProxy*>>& PreprocessingPasses = GetRenderWork().PreprocessingPasses;

	if (PreprocessingPasses.IsEmpty())
	{
		return;
	}

	FPassContext PassContext = {};
	const FScopedExternalTextureMap ExternalTextures{ *this, GraphBuilder, InView };

	for (const TPair<ResourceId, TArray<const FCompositeCorePassProxy*>>& Pair : PreprocessingPasses)
	{
		const ResourceId ExternalId = Pair.Key;
		const TArray<const FCompositeCorePassProxy*>& Passes = Pair.Value;
		const FPassInput* ExternalTexture = ExternalTextures.Get().Find(ExternalId);

		if (!ensureMsgf(ExternalTexture != nullptr, TEXT("Unexpected missing external texture.")))
		{
			continue;
		}

		// Setup pass inputs
		FPassInputArray PassInputs;
		PassInputs.GetArray().Add(*ExternalTexture);

		// Iterate over all passes
		for (int32 PassIndex =0; PassIndex <Passes.Num(); ++PassIndex)
		{
			const FCompositeCorePassProxy* Pass = Passes[PassIndex];
			const TOptional<UE::CompositeCore::ResourceId>& OverrrideOutput = Pass->GetDeclaredOutputOverride();
			bool bPassOutputOverridden = false;

			if (OverrrideOutput.IsSet())
			{
				const FPassInput* ExternalOutputOverride = ExternalTextures.Get().Find(OverrrideOutput.GetValue());
				if (ensureMsgf(ExternalOutputOverride != nullptr, TEXT("Unexpected missing external render target override as output.")))
				{
					PassInputs.OverrideOutput = FScreenPassRenderTarget(ExternalOutputOverride->Texture, ERenderTargetLoadAction::ENoAction);
					
					// Update view rect to match external texture
					PassContext.OutputViewRect = ExternalOutputOverride->Texture.ViewRect;
					// Mark as overriden
					bPassOutputOverridden = true;
				}
			}
			

			if(!bPassOutputOverridden)
			{
				PassInputs.OverrideOutput = FScreenPassRenderTarget{};

				// Update view rect to match external texture
				PassContext.OutputViewRect = ExternalTexture->Texture.ViewRect;
			}

			// Register pass and update output
			const FPassTexture Output = Pass->Add(GraphBuilder, InView, PassInputs, PassContext);

			// Note: we do not cache the output to CachedOutputsPerView_RenderThread since we only support cached outputs in post-processing for now.

			// Update next pass input
			PassInputs[0] = Output;
		}
	}
}

UE::CompositeCore::FScopedExternalTextureMap::FScopedExternalTextureMap(const FCompositeCoreSceneViewExtension& SVE, FRDGBuilder& InGraphBuilder, const FSceneView& InView)
	: GraphBuilder{InGraphBuilder}
{
	using namespace UE::CompositeCore;

	Textures.Reserve(2 + SVE.ExternalInputs_RenderThread.Num());

	{
		FPassInput& Resource = Textures.Add(BUILT_IN_EMPTY_ID);
		Resource.Texture = FScreenPassTexture{ GSystemTextures.GetBlackDummy(GraphBuilder) };
	}

	{
		FPassInput& Resource = Textures.Add(BUILT_IN_CRP_ID);

		const TRefCountPtr<IPooledRenderTarget>* CompositeRenderPassPtr = SVE.CustomRenderTargetPerView_RenderThread.Find(InView.GetViewKey());
		if (CompositeRenderPassPtr != nullptr)
		{
			Resource.Texture = FScreenPassTexture{ GraphBuilder.RegisterExternalTexture(*CompositeRenderPassPtr) };
			Resource.Metadata.bInvertedAlpha = true;
		}
		else
		{
			// Still provide an empty CRP target
			Resource.Texture = FScreenPassTexture{ GSystemTextures.GetBlackDummy(GraphBuilder) };
		}
	}

	RestoreToExternal.Reserve(SVE.ExternalInputs_RenderThread.Num());

	for (const auto& Pair : SVE.ExternalInputs_RenderThread)
	{
		const TRefCountPtr<IPooledRenderTarget>& PooledRenderTarget = Pair.Value.RenderTarget;
		FRDGTextureRef TextureResource = GraphBuilder.RegisterExternalTexture(PooledRenderTarget);
		
		FPassInput& Resource = Textures.Add(Pair.Key);
		Resource.Texture = FScreenPassTexture{ TextureResource };
		Resource.Metadata = Pair.Value.Metadata;

		/**
		 * Mark for internal access and store to reset in destructor.
		 * 
		 * Note: we exclude the active view family render target since it is already (and should remain) internal.
		 * In practice, this can occur when our textures are accessed by pre-processing passes during a scene capture render.
		 * 
		 * Unfortunately, full AccessModeState information is not accessible outside of RDG or friend classes. See for example
		 * GraphBuilder.UseExternalAccessMode(Buffer, AccessModeState.Access, AccessModeState.Pipelines); use in FRDGResourceDumpContext.
		 */
		FRHITexture* FamilyRenderTargetRHI = InView.Family->RenderTarget ? InView.Family->RenderTarget->GetRenderTargetTexture().GetReference() : nullptr;
		if (TextureResource && (PooledRenderTarget->GetRHI() != FamilyRenderTargetRHI))
		{
			GraphBuilder.UseInternalAccessMode(TextureResource);

			RestoreToExternal.Add(TextureResource);
		}
	}
}

UE::CompositeCore::FScopedExternalTextureMap::~FScopedExternalTextureMap()
{
	for (FRDGViewableResource* TextureResource : RestoreToExternal)
	{
		GraphBuilder.UseExternalAccessMode(TextureResource, ERHIAccess::SRVMask);
	}
}

bool FCompositeCoreSceneViewExtension::ApplyPasses_Recursive(
	FRDGBuilder& GraphBuilder,
	const FSceneView& InView,
	const UE::CompositeCore::FPassInputArray& Inputs,
	const UE::CompositeCore::FPassInputArray& OriginalInputs,
	UE::CompositeCore::FPassContext& PassContext,
	const TArray<const FCompositeCorePassProxy*> InPasses,
	const UE::CompositeCore::FScopedExternalTextureMap& InExternalTextures,
	const int32 LastMergePassIndex,
	const int32 RecursionLevel,
	UE::CompositeCore::FPassTexture& Output)
{
	using namespace UE::CompositeCore;

	if (InPasses.IsEmpty())
	{
		return false;
	}

	// Default pass inputs
	FPassInputArray BasePassInputs = Inputs;
	const TSortedMap<const FCompositeCorePassProxy*, const UE::CompositeCore::FPassTexture>& CachedOutputs = CachedOutputsPerView_RenderThread.FindOrAdd(InView.GetViewKey());
	
	// Iterate over all passes
	for (int32 PassIndex = 0; PassIndex < InPasses.Num(); ++PassIndex)
	{
		const FCompositeCorePassProxy* Pass = InPasses[PassIndex];
		
		// Last merge pass is used to output values compatible with scene color
		bool bIsLastMergePass = false;
		// Last pass writes to the viewport render target
		bool bIsLastPass = false;
		
		if (RecursionLevel == 0)
		{
			bIsLastMergePass = (PassIndex == LastMergePassIndex);
			bIsLastPass = (PassIndex == InPasses.Num() - 1);
		}

		// Update inputs for the current pass
		FPassInputArray PassInputs = BasePassInputs;

		// Iterate over declared pass inputs
		for (int32 InputIndex = 0; InputIndex < Pass->GetNumDeclaredInputs(); ++InputIndex)
		{
			const FPassInputDecl& DeclaredInput = Pass->GetDeclaredInput(InputIndex);

			// Input is the output result of a child (sub)pass
			if (DeclaredInput.IsType<const FCompositeCorePassProxy*>())
			{
				const FCompositeCorePassProxy* ChildPassProxy = DeclaredInput.Get<const FCompositeCorePassProxy*>();
				UE::CompositeCore::FPassTexture ChildPassOutput;

				// We first check if the input proxy has already produced an output (and let RDG do the rest!)
				if (const UE::CompositeCore::FPassTexture* PreProducedOutput = CachedOutputs.Find(ChildPassProxy))
				{
					PassInputs[InputIndex] = *PreProducedOutput;
				}
				else if(ApplyPasses_Recursive(GraphBuilder, InView, PassInputs, OriginalInputs, PassContext, { ChildPassProxy }, InExternalTextures, INDEX_NONE, RecursionLevel + 1, ChildPassOutput))
				{
					PassInputs[InputIndex] = ChildPassOutput;
				}
			}
			// If an external texture resource is expected, connect the current input index with the resolved identifier
			else if (DeclaredInput.IsType<FPassExternalResourceDesc>())
			{
				const FPassExternalResourceDesc& Desc = DeclaredInput.Get<FPassExternalResourceDesc>();
				const ResourceId DeclaredExternalId = Desc.Id;

				const FPassInput* SliceInput = InExternalTextures.Get().Find(DeclaredExternalId);
				if (ensureMsgf(SliceInput, TEXT("Invalid external input: %d"), DeclaredExternalId))
				{
					PassInputs[InputIndex] = *SliceInput;
				}
				else
				{
					UE_CALL_ONCE([]()
						{
							UE_LOG(LogCompositeCore, Warning, TEXT("Compositing may be incorrectly running inside a scene capture render, and trying to use render targets which have not yet been updated. Consider disabling AllowPrimitiveAlphaHoldout on your scene capture(s)."))
						});
				}
			}
			// If an internal texture resource is expected, connect the current input index with either the original or current bass pass input.
			else if (DeclaredInput.IsType<FPassInternalResourceDesc>())
			{
				const FPassInternalResourceDesc& Desc = DeclaredInput.Get<FPassInternalResourceDesc>();
				const int32 DeclaredInputIndex = Desc.Index;

				if (Desc.bOriginalCopyBeforePasses)
				{
					if (ensureMsgf(OriginalInputs.IsValidIndex(DeclaredInputIndex), TEXT("Invalid internal input: %d"), DeclaredInputIndex))
					{
						PassInputs[InputIndex] = OriginalInputs[DeclaredInputIndex];
					}
				}
				else
				{
					if (ensureMsgf(BasePassInputs.IsValidIndex(DeclaredInputIndex), TEXT("Invalid internal input: %d"), DeclaredInputIndex))
					{
						PassInputs[InputIndex] = BasePassInputs[DeclaredInputIndex];
					}
				}
			}
			else
			{
				checkNoEntry();
			}
		}

		if (bIsLastMergePass)
		{
			// Merge pass proxies will handle alpha inversion, lens distortion and color encodings when writing back to scene color
			PassContext.bOutputSceneColor = true;
		}

		if (!bIsLastPass)
		{
			// Only apply the output override on the last pass
			PassInputs.OverrideOutput = FScreenPassRenderTarget{};
		}

		// Register pass and update output
		Output = Pass->Add(GraphBuilder, InView, PassInputs, PassContext);
		
		// Cache the pass output per view
		CachedOutputsPerView_RenderThread.FindOrAdd(InView.GetViewKey()).Add(Pass, Output);

		BasePassInputs[0] = Output;
	}

	return true;
}

FScreenPassTexture FCompositeCoreSceneViewExtension::PostProcessWork_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& InView, const FPostProcessMaterialInputs& Inputs, ISceneViewExtension::EPostProcessingPass InLocation)
{
	using namespace UE::CompositeCore;

	const FScopedExternalTextureMap ExternalTextures{ *this, GraphBuilder, InView };
	const TArray<const FCompositeCorePassProxy*>& Passes = GetRenderWork().FramePasses[InLocation];

	FPassContext PassContext;
	PassContext.SceneTextures = Inputs.SceneTextures;
	PassContext.OutputViewRect = Inputs.GetInput(EPostProcessMaterialInput::SceneColor).ViewRect;
	PassContext.Location = InLocation;
	PassContext.bOutputSceneColor = false;

	FPassInputArray ResolvedInputs(GraphBuilder, InView, Inputs, InLocation);
	FPassTexture Output;

	const int32 LastMergePassIndex = Passes.FindLastByPredicate([](const FCompositeCorePassProxy* InPass)
		{
			return InPass ? (InPass->GetTypeName() == GetMergePassPassName()) : false;
		});
	constexpr int32 RecursionLevel = 0;
	
	if (ApplyPasses_Recursive(GraphBuilder, InView, ResolvedInputs, ResolvedInputs, PassContext, Passes, ExternalTextures, LastMergePassIndex, RecursionLevel, Output))
	{
		return MoveTemp(Output.Texture);
	}
	else
	{
		return Inputs.ReturnUntouchedSceneColorForPostProcessing(GraphBuilder);
	}
}

void FCompositeCoreSceneViewExtension::PostRenderViewFamily_RenderThread(FRDGBuilder& GraphBuilder, FSceneViewFamily& InViewFamily)
{

}

void FCompositeCoreSceneViewExtension::PostRenderView_RenderThread(FRDGBuilder& GraphBuilder, FSceneView& InView)
{
	CustomRenderTargetPerView_RenderThread.Remove(InView.GetViewKey());
}

// Copyright Epic Games, Inc. All Rights Reserved.

#include "Render/GUILayer/DisplayClusterGuiLayerController.h"

#include "IDisplayClusterShaders.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"

#include "Async/Async.h"
#include "Framework/Application/SlateApplication.h"
#include "Engine/Console.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Misc/DisplayClusterGlobals.h"
#include "Misc/DisplayClusterLog.h"
#include "Render/Viewport/IDisplayClusterViewportProxy.h"
#include "ShaderParameters/DisplayClusterShaderParameters_Overlay.h"
#include "Slate/SceneViewport.h"


TAutoConsoleVariable<bool> CVarPropagateGui(
	TEXT("nDisplay.GUI.Propagate"),
	false,
	TEXT("Show GUI on viewports\n")
	TEXT("0 : Disabled\n")
	TEXT("1 : Enabled\n"),
	ECVF_RenderThreadSafe
);


FDisplayClusterGuiLayerController::FDisplayClusterGuiLayerController()
	: bEnabled(GDisplayCluster ? GDisplayCluster->GetOperationMode() == EDisplayClusterOperationMode::Cluster : false)
{
	if (bEnabled)
	{
		// GameThread only intialization
		AsyncTask(ENamedThreads::GameThread, [this]()
			{
				FSlateApplication::Get().OnPreTick().AddRaw(this, &FDisplayClusterGuiLayerController::HandleSlatePreTick);
			});
	}
}

FDisplayClusterGuiLayerController& FDisplayClusterGuiLayerController::Get()
{
	static FDisplayClusterGuiLayerController Instance;
	return Instance;
}

FRDGTextureRef FDisplayClusterGuiLayerController::ProcessFinalTexture_RenderThread(FRDGBuilder& GraphBuilder, FRDGTextureRef FinalTexture)
{
	checkSlow(IsInRenderingThread());
	checkSlow(FinalTexture);

	// Nothing to do if GUI layer propagation is not active
	if (!bActiveThisFrameRT || !bEnabled)
	{
		return FinalTexture;
	}

	// Make sure our buffer duplicate is valid
	if (!TexViewportOriginalRHI.IsValid())
	{
		return FinalTexture;
	}

	// Create an RDG texture reference to the original game viewport's texture that we stored before Slate rendering
	FRDGTextureRef Output = RegisterExternalTexture(GraphBuilder, TexViewportOriginalRHI.GetReference(), TEXT("nD.TexViewportOriginalRHI"));
	if (!Output)
	{
		return FinalTexture;
	}

	// If everything Ok, return the original viewport's buffer
	return Output;
}

void FDisplayClusterGuiLayerController::ProcessPostRenderViewFamily_RenderThread(FRDGBuilder& GraphBuilder, const FSceneViewFamily& ViewFamiliy, const IDisplayClusterViewportProxy* ViewportProxy)
{
	checkSlow(IsInRenderingThread());
	checkSlow(ViewportProxy);

	// Nothing to do if GUI layer propagation is not active
	if (!bActiveThisFrameRT || !bEnabled)
	{
		return;
	}

	// Validate both input and internal data
	if (!ViewportProxy || !TexViewportOriginalRHI.IsValid() || !TexViewportGUILayerRHI.IsValid())
	{
		return;
	}

	TArray<FRHITexture*> Textures;
	TArray<FIntRect>     Regions;

	// Get nD viewport's buffer
	if (ViewportProxy->GetResourcesWithRects_RenderThread(EDisplayClusterViewportResourceType::InternalRenderTargetEntireRectResource, Textures, Regions))
	{
		if (Textures.Num() > 0 && Regions.Num() > 0 && Textures[0])
		{
			const FIntPoint DCViewportSize = Regions[0].Size();

			// Prepare textures for drawing
			FRDGTextureRef TexViewportGUILayerRDG = RegisterExternalTexture(GraphBuilder, TexViewportGUILayerRHI, TEXT("nD.TexViewportGUILayerRHI"));
			FRDGTextureRef TexDCViewportRDG       = RegisterExternalTexture(GraphBuilder, Textures[0], *Textures[0]->GetName().ToString());
			FRDGTextureRef TexTempDCViewportRDG   = CreateTextureFrom_RenderThread(GraphBuilder, Textures[0], TEXT("nD.TexTempDCViewportRDG"), DCViewportSize);

			// Make a copy to the input texture
			FRHICopyTextureInfo CopyInfo;
			CopyInfo.Size = FIntVector{ DCViewportSize.X, DCViewportSize.Y, 1 };
			CopyInfo.SourcePosition = FIntVector{ Regions[0].Min.X, Regions[0].Min.Y, 0 };
			AddCopyTexturePass(GraphBuilder, TexDCViewportRDG, TexTempDCViewportRDG, CopyInfo);

			FDisplayClusterShaderParameters_Overlay Parameters;
			Parameters.OverlayTexture = TexViewportGUILayerRDG; // GUI layer
			Parameters.BaseTexture    = TexTempDCViewportRDG;   // Scene color (copy of nD viewport)
			Parameters.OutputTexture  = TexDCViewportRDG;       // Scene color (original nD viewport)

			// Draw the GUI layer on top of the nD viewport
			IDisplayClusterShaders::Get().AddDrawOverlayPass(GraphBuilder, Parameters);
		}
	}
}

void FDisplayClusterGuiLayerController::HandleSlatePreTick(float InDeltaTime)
{
	checkSlow(IsInGameThread());

	// Nothing to do if disabled
	if (!bEnabled)
	{
		return;
	}

	// Read the cvar once per frame
	bActiveThisFrame = CVarPropagateGui.GetValueOnGameThread();

	ENQUEUE_RENDER_COMMAND(FlushCanvasRT)(
		[this, bNewActiveThisFrame = bActiveThisFrame](FRHICommandListImmediate& RHICmdList)
		{
			// Update the corresponding flag, and leave if inactive
			bActiveThisFrameRT = bNewActiveThisFrame;
			if (!bActiveThisFrameRT)
			{
				return;
			}

			// Get game viewport's current render target
			FTextureRHIRef ViewportRTT = GetViewportRTT_RenderThread();
			if (!ViewportRTT.IsValid())
			{
				return;
			}

			// Create a buffer duplicate, but float16
			UpdateTempTexture_RenderThread(RHICmdList, TexViewportGUILayerRHI, ViewportRTT, TEXT("nD.TexViewportGUILayerRHI"), PF_FloatRGBA);
			if (!TexViewportGUILayerRHI.IsValid())
			{
				return;
			}

			// Store the original vieport's buffer, and substitute is with our float16 buffer
			TexViewportOriginalRHI = ViewportRTT;
			SetViewportRTT_RenderThread(TexViewportGUILayerRHI);

			// Clear out texture to transparent to let Slate renderer produce a clear GUI layer
			FRDGBuilder GraphBuilder(RHICmdList);
			FRDGTextureRef TexViewportGUILayerRDG = RegisterExternalTexture(GraphBuilder, TexViewportGUILayerRHI.GetReference(), TEXT("nD.TexViewportGUILayerRDG"));
			AddClearRenderTargetPass(GraphBuilder, TexViewportGUILayerRDG, FLinearColor::Transparent);

			GraphBuilder.Execute();
		});
}

FTextureRHIRef FDisplayClusterGuiLayerController::GetViewportRTT_RenderThread()
{
	checkSlow(IsInRenderingThread());

	if (GEngine && GEngine->GameViewport && GEngine->GameViewport->Viewport)
	{
		return GEngine->GameViewport->Viewport->GetRenderTargetTexture();
	}

	return nullptr;
}

void FDisplayClusterGuiLayerController::SetViewportRTT_RenderThread(FTextureRHIRef& NewRTT)
{
	checkSlow(IsInRenderingThread());
	checkSlow(NewRTT.IsValid());

	if (NewRTT.IsValid())
	{
		if (GEngine && GEngine->GameViewport && GEngine->GameViewport->Viewport)
		{
			static_cast<FSceneViewport*>(GEngine->GameViewport->Viewport)->SetRenderTargetTextureRenderThread(NewRTT);
		}
	}
}

void FDisplayClusterGuiLayerController::UpdateTempTexture_RenderThread(
	FRHICommandListImmediate& RHICmdList,
	FTextureRHIRef& StorageTexture,
	FTextureRHIRef& FromTexture,
	const TCHAR* Name,
	EPixelFormat PixelFormat
)
{
	if (!FromTexture.IsValid())
	{
		return;
	}

	// Create new one if not exists
	if (!StorageTexture.IsValid())
	{
		StorageTexture = CreateTextureFrom_RenderThread(RHICmdList, FromTexture, Name, PixelFormat);
	}

	// Re-create if different size
	if (FromTexture->GetDesc().Extent != StorageTexture->GetDesc().Extent)
	{
		StorageTexture = CreateTextureFrom_RenderThread(RHICmdList, FromTexture, Name, PixelFormat);
	}
}

FTextureRHIRef FDisplayClusterGuiLayerController::CreateTextureFrom_RenderThread(FRHICommandListImmediate& RHICmdList, FTextureRHIRef& FromTexture, const TCHAR* Name, EPixelFormat PixelFormat)
{
	if (!FromTexture.IsValid())
	{
		return nullptr;
	}

	// Use original format and size
	const EPixelFormat Format = (PixelFormat != PF_Unknown ? PixelFormat : FromTexture->GetDesc().Format);
	const FIntPoint Size = FromTexture->GetDesc().Extent;

	// Prepare description
	FRHITextureCreateDesc Desc =
		FRHITextureCreateDesc::Create2D(Name, Size.X, Size.Y, Format)
		.SetClearValue(FClearValueBinding::Transparent)
		.SetNumMips(1)
		.SetFlags(ETextureCreateFlags::ShaderResource | ETextureCreateFlags::RenderTargetable)
		.SetInitialState(ERHIAccess::SRVMask);

	// Create texture
	return RHICreateTexture(Desc);
}

FRDGTextureRef FDisplayClusterGuiLayerController::CreateTextureFrom_RenderThread(
	FRDGBuilder& GraphBuilder,
	const TCHAR* Name,
	const FIntPoint& Size,
	EPixelFormat PixelFormat,
	ETextureCreateFlags CreateFlags = TexCreate_ShaderResource | TexCreate_RenderTargetable,
	const FClearValueBinding& ClearValueBinding = FClearValueBinding::Transparent)
{
	const FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(Size, PixelFormat, ClearValueBinding, CreateFlags);
	return GraphBuilder.CreateTexture(Desc, Name);
}

FRDGTextureRef FDisplayClusterGuiLayerController::CreateTextureFrom_RenderThread(FRDGBuilder& GraphBuilder, FRDGTextureRef& FromTexture, const TCHAR* Name, const FIntPoint& Size)
{
	if (!FromTexture)
	{
		return nullptr;
	}

	const FIntPoint NewSize = (Size == FIntPoint::ZeroValue ? FromTexture->Desc.Extent : Size);
	return CreateTextureFrom_RenderThread(GraphBuilder, Name, NewSize, FromTexture->Desc.Format);
}

FRDGTextureRef FDisplayClusterGuiLayerController::CreateTextureFrom_RenderThread(FRDGBuilder& GraphBuilder, FRHITexture* FromTexture, const TCHAR* Name, const FIntPoint& Size)
{
	if (!FromTexture)
	{
		return nullptr;
	}

	const FIntPoint NewSize = (Size == FIntPoint::ZeroValue ? FromTexture->GetDesc().Extent : Size);
	return CreateTextureFrom_RenderThread(GraphBuilder, Name, NewSize, FromTexture->GetFormat());
}

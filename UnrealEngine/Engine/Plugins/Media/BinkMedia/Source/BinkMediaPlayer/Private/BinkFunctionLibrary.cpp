// Copyright Epic Games Tools LLC
//   Licenced under the Unreal Engine EULA 

#include "BinkFunctionLibrary.h"

#include "BinkMovieStreamer.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Engine.h"
#include "RenderingThread.h"



extern TSharedPtr<FBinkMovieStreamer, ESPMode::ThreadSafe> MovieStreamer;

// Note: Has to be in a seperate function because you can't do #if inside a render command macro
static void Bink_DrawOverlays_Internal(FRHICommandListImmediate &RHICmdList, FTextureRHIRef BackBuffer, FVector2D ScreenSize) {
	if(!BackBuffer.GetReference()) 
	{
		return;
	}

	BINKPLUGINFRAMEINFO FrameInfo = {};
	FrameInfo.screen_resource = BackBuffer.GetReference();
	FrameInfo.screen_resource_state = 4; // D3D12_RESOURCE_STATE_RENDER_TARGET; (only used in d3d12)
	FrameInfo.width = ScreenSize.X;
	FrameInfo.height = ScreenSize.Y;
	FrameInfo.sdr_or_hdr = BackBuffer->GetFormat() == PF_A2B10G10R10 ? 1 : 0;
	FrameInfo.cmdBuf = &RHICmdList;
	BinkPluginSetPerFrameInfo(&FrameInfo);
	BinkPluginAllScheduled();
	BinkPluginDraw(0, 1);
}

void UBinkFunctionLibrary::Bink_DrawOverlays() 
{
	if (!GEngine || !GEngine->GameViewport || !GEngine->GameViewport->Viewport) {
		return;
	}

	FVector2D ScreenSize;
	GEngine->GameViewport->GetViewportSize(ScreenSize);
	FTextureRHIRef BackBuffer = GEngine->GameViewport->Viewport->GetRenderTargetTexture();

	if (!BackBuffer.IsValid()) {
		return;
	}

	ENQUEUE_RENDER_COMMAND(BinkOverlays)([ScreenSize, BackBuffer](FRHICommandListImmediate& RHICmdList) 
	{ 
		Bink_DrawOverlays_Internal(RHICmdList, BackBuffer, ScreenSize);
	});
}

FTimespan UBinkFunctionLibrary::BinkLoadingMovie_GetDuration() 
{
	double ms = 0;
	if(MovieStreamer.IsValid() && MovieStreamer.Get()->bnk) 
	{
		BINKPLUGININFO bpinfo = {};
		BinkPluginInfo(MovieStreamer.Get()->bnk, &bpinfo);
		ms = ((double)bpinfo.Frames) * ((double)bpinfo.FrameRateDiv) * 1000.0 / ((double)bpinfo.FrameRate);
	}
	return FTimespan::FromMilliseconds(ms);
}

FTimespan UBinkFunctionLibrary::BinkLoadingMovie_GetTime() 
{
	double ms = 0;
	if(MovieStreamer.IsValid() && MovieStreamer.Get()->bnk) 
	{
		BINKPLUGININFO bpinfo = {};
		BinkPluginInfo(MovieStreamer.Get()->bnk, &bpinfo);
		ms = ((double)bpinfo.FrameNum) * ((double)bpinfo.FrameRateDiv) * 1000.0 / ((double)bpinfo.FrameRate);
	}
	return FTimespan::FromMilliseconds(ms);
}

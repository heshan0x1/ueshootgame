// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PixelFormat.h"
#include "RHIResources.h"
#include "UnrealClient.h"


class FRDGBuilder;
class FSceneViewFamily;
class IDisplayClusterViewportProxy;


/**
 * GUI layer controller singleton
 * 
 * This class is responsible for propagation of the GUI layer (includes canvases + UMG) across
 * all nDisplay viewports.
 */
class FDisplayClusterGuiLayerController
{
protected:
	FDisplayClusterGuiLayerController();
	~FDisplayClusterGuiLayerController() = default;

public:
	static FDisplayClusterGuiLayerController& Get();

public:

	/**
	 * Returns true if this controller is enabled
	 */
	bool IsEnabled() const
	{
		return bEnabled;
	}

	/**
	 * Returns true if this controller is active this frame
	 */
	bool IsActiveThisFrame() const
	{
		checkSlow(IsInGameThread() || IsInRenderingThread());
		const bool bActive = (IsInRenderingThread() ? bActiveThisFrameRT : bActiveThisFrame);
		return IsEnabled() && bActive;
	}

	/**
	 * Called before copying the final output to the backbuffer. It's used to substitute the current
	 * viewport's buffer to its original one stored on SlatePreTick (refer corresponding method).
	 * 
	 * @param FinalTexture - The final Slate outcome that is passed for copying to the backbuffer
	 */
	FRDGTextureRef ProcessFinalTexture_RenderThread(FRDGBuilder& GraphBuilder, FRDGTextureRef FinalTexture);

	/**
	 * Called for each nDisplay view family once rendering is finished. Responsible for drawing
	 * the GUI layer on top of every nDisplay viewport.
	 * 
	 * @param ViewFamily - View family that was just rendered
	 * @param ViewProxy  - nDisplay viewport (render thread obj)
	 */
	void ProcessPostRenderViewFamily_RenderThread(FRDGBuilder& GraphBuilder, const FSceneViewFamily& ViewFamiliy, const IDisplayClusterViewportProxy* ViewportProxy);

private:

	/**
	 * Slate pre-Tick callback, used to substitute original game viewport buffer with our custom texture
	 * to store whole Slate rendering output separately, and with high-precision alpha.
	 */
	void HandleSlatePreTick(float InDeltaTime);

private:

	/** Returns current game vieport buffer */
	FTextureRHIRef GetViewportRTT_RenderThread();

	/** Sets up new game viewport bufer (stays there until end of the frame) */
	void SetViewportRTT_RenderThread(FTextureRHIRef& NewRTT);

	/**
	 * Creates or re-creates a texture duplicate from a referenced texture. Allows to override some texture parameters.
	 * If some parameters of the referenced texture are different, a new internal texture will be created.
	 * 
	 * @param StorageTexture - An internal "duplicate" of the referenced texture
	 * @param FromTexture    - The referenced texture
	 * @param Name           - Name of the new texture
	 * @param PixelFormat    - Pixel format of the new texture
	 *
	*/
	void UpdateTempTexture_RenderThread(FRHICommandListImmediate& RHICmdList, FTextureRHIRef& StorageTexture, FTextureRHIRef& FromTexture, const TCHAR* Name, EPixelFormat PixelFormat = PF_Unknown);

	/** Auxiliary function to create an RHI texture using another RHI texture as a template, overriding some of the reference parameters. */
	FTextureRHIRef CreateTextureFrom_RenderThread(FRHICommandListImmediate& RHICmdList, FTextureRHIRef& FromTexture, const TCHAR* Name, EPixelFormat PixelFormat = PF_Unknown);

	/** Auxiliary function to create an RDG texture. */
	FRDGTextureRef CreateTextureFrom_RenderThread(FRDGBuilder& GraphBuilder, const TCHAR* Name, const FIntPoint& Size, EPixelFormat PixelFormat, ETextureCreateFlags CreateFlags, const FClearValueBinding& ClearValueBinding);

	/** Auxiliary function to create an RDG texture from an RDG texture template, overriding some of the reference parameters. */
	FRDGTextureRef CreateTextureFrom_RenderThread(FRDGBuilder& GraphBuilder, FRDGTextureRef& FromTexture, const TCHAR* Name, const FIntPoint& Size = FIntPoint::ZeroValue);

	/** Auxiliary function to create an RDG texture from an RHI texture template, overriding some of the reference parameters. */
	FRDGTextureRef CreateTextureFrom_RenderThread(FRDGBuilder& GraphBuilder, FRHITexture* FromTexture, const TCHAR* Name, const FIntPoint& Size = FIntPoint::ZeroValue);

private:

	/** Whether GUI layer propagation is active within current session */
	const bool bEnabled;

	/** Whether GUI layer propagation is active within current frame (game thread) */
	bool bActiveThisFrame = false;

	/** Whether GUI layer propagation is active within current frame (render thread) */
	bool bActiveThisFrameRT = false;

	/** The RHI texture reference to the original game viewport buffer  */
	FTextureRHIRef TexViewportOriginalRHI;

	/** Internal RHI texture used by Slate to draw UI. */
	FTextureRHIRef TexViewportGUILayerRHI;
};

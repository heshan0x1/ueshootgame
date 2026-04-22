// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "StereoLayerManager.h"
#include "SceneViewExtension.h"

#define UE_API XRBASE_API
class FHeadMountedDisplayBase;

/** Experimental struct */
struct FDefaultStereoLayers_LayerRenderParams
{
	FIntRect Viewport;
	FMatrix RenderMatrices[3];
};

/** 
 *	Default implementation of stereo layers for platforms that require emulating layer support.
 *
 *	FHeadmountedDisplayBase subclasses will use this implementation by default unless overridden.
 */
class FDefaultStereoLayers : public FSimpleLayerManager, public FHMDSceneViewExtension
{
public:
	UE_API FDefaultStereoLayers(const FAutoRegister& AutoRegister, FHeadMountedDisplayBase* InHMDDevice);

	/** ISceneViewExtension interface */
	UE_API virtual void SetupViewFamily(FSceneViewFamily& InViewFamily) override;
	UE_API virtual void BeginRenderViewFamily(FSceneViewFamily& InViewFamily) override;
	UE_API virtual void PostRenderView_RenderThread(FRDGBuilder& GraphBuilder, FSceneView& InView) override;

	/** IStereoLayers interface */
	UE_API virtual TArray<FTextureRHIRef, TInlineAllocator<2>> GetDebugLayerTextures_RenderThread() override;
	UE_API TArray<FTextureRHIRef, TInlineAllocator<2>> GetDebugLayerTexturesImpl_RenderThread();
	UE_API virtual void GetAllocatedTexture(uint32 LayerId, FTextureRHIRef& Texture, FTextureRHIRef& LeftTexture) override final;

	/** Experimental method */
	struct FStereoLayerToRenderTransfer
	{
		uint32 Id;
		int32 Priority;
		uint32 Flags;
		ELayerType PositionType;
		FVector2D QuadSize;
		FBox2D UVRect;
		FTransform Transform;
		FTextureResource* Texture;
		FTextureRHIRef Texture_Deprecated;

		XRBASE_API FStereoLayerToRenderTransfer(const FLayerDesc& Desc);
		FStereoLayerToRenderTransfer(const FStereoLayerToRenderTransfer&) = default;
	};
	struct FStereoLayerToRender
	{
		uint32 Id;
		int32 Priority;
		uint32 Flags;
		ELayerType PositionType;
		FVector2D QuadSize;
		FBox2D UVRect;
		FTransform Transform;
		FTextureRHIRef Texture;

		XRBASE_API FStereoLayerToRender(const FStereoLayerToRenderTransfer& Transfer);
		FStereoLayerToRender(const FStereoLayerToRender&) = default;
		FStereoLayerToRender(FStereoLayerToRender&&) = default;
	};
	static UE_API void StereoLayerRender(FRHICommandListImmediate& RHICmdList, TArrayView<const FStereoLayerToRender> LayersToRender, const FDefaultStereoLayers_LayerRenderParams& RenderParams);

protected:
	
	/**
	 * Invoked by FHeadMountedDisplayBase to update the HMD position during the late update.
	 */
	void UpdateHmdTransform(const FTransform& InHmdTransform)
	{
		HmdTransform = InHmdTransform;
	}

	FHeadMountedDisplayBase* HMDDevice;
	FTransform HmdTransform;

	TArray<FStereoLayerToRender> SortedSceneLayers;
	TArray<FStereoLayerToRender> SortedOverlayLayers;
	bool bClearLayerBackground = false;

	friend class FHeadMountedDisplayBase;
};

#undef UE_API

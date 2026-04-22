// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IHeadMountedDisplay.h"
#include "XRTrackingSystemBase.h"
#include "DefaultSpectatorScreenController.h"
#include "RenderGraphFwd.h"

#define UE_API XRBASE_API

/**
 * Default implementation for various IHeadMountedDisplay methods.
 * You can extend this class instead of IHeadMountedDisplay directly when implementing support for new HMD devices.
 */

class FHeadMountedDisplayBase : public FXRTrackingSystemBase, public IHeadMountedDisplay, public IStereoRendering
{

public:
	UE_API FHeadMountedDisplayBase(IARSystemSupport* InARImplementation);
	virtual ~FHeadMountedDisplayBase() {}

	/**
	 * Retrieves the HMD name, by default this is the same as the system name.
	 */
	virtual FName GetHMDName() const override { return GetSystemName(); }

	/**
	 * Record analytics - To add custom information logged with the analytics, override PopulateAnalyticsAttributes
	 */
	UE_API virtual void RecordAnalytics() override;

	/** 
	 * Default IXRTrackingSystem implementation
	 */
	UE_API virtual bool IsHeadTrackingAllowed() const override;

	/** Optional IXRTrackingSystem methods.
	  */
	UE_API virtual bool IsHeadTrackingEnforced() const override;
	UE_API virtual void SetHeadTrackingEnforced(bool bEnabled) override;

	/** 
	 * Default stereo layer implementation
	 */
	UE_API virtual IStereoLayers* GetStereoLayers() override;

	UE_API virtual bool GetHMDDistortionEnabled(EShadingPath ShadingPath) const override;

	UE_API virtual void OnLateUpdateApplied_RenderThread(FRDGBuilder& GraphBuilder, const FTransform& NewRelativeTransform) override;

	UE_API virtual void CalculateStereoViewOffset(const int32 ViewIndex, FRotator& ViewRotation, const float WorldToMeters, FVector& ViewLocation) override;
	UE_API virtual void InitCanvasFromView(FSceneView* InView, UCanvas* Canvas) override;

	UE_API virtual bool IsSpectatorScreenActive() const override;

	UE_API virtual class ISpectatorScreenController* GetSpectatorScreenController() override;
	UE_API virtual class ISpectatorScreenController const* GetSpectatorScreenController() const override;

	// Spectator Screen Hooks into specific implementations
	// Get the point on the left eye render target which the viewers eye is aimed directly at when looking straight forward. 0,0 is top left.
	UE_API virtual FVector2D GetEyeCenterPoint_RenderThread(const int32 ViewIndex) const;
	// Get the rectangle of the HMD rendertarget for the left eye which seems undistorted enough to be cropped and displayed on the spectator screen.
	virtual FIntRect GetFullFlatEyeRect_RenderThread(const FRHITextureDesc& EyeTexture) const { return FIntRect(0, 0, 1, 1); }
	UE_DEPRECATED(5.6, "Use the FRHITextureDesc overload instead")
	virtual FIntRect GetFullFlatEyeRect_RenderThread(FTextureRHIRef EyeTexture) const final { return FIntRect(0, 0, 1, 1); }
	// Helper to copy one render target into another for spectator screen display
	UE_DEPRECATED(5.6, "Call ::AddXRCopyTexturePass instead")
	UE_API virtual void CopyTexture_RenderThread(FRHICommandListImmediate& RHICmdList, FRHITexture* SrcTexture, FIntRect SrcRect, FRHITexture* DstTexture, FIntRect DstRect, bool bClearBlack, bool bNoAlpha) const;

protected:
	/**
	 * Called by RecordAnalytics when creating the analytics event sent during HMD initialization.
	 *
	 * Return false to disable recording the analytics event	
	 */
	UE_API virtual bool PopulateAnalyticsAttributes(TArray<struct FAnalyticsEventAttribute>& EventAttributes);

	/**
	 * Implement this method to provide an alternate render target for head locked stereo layer rendering, when using the default Stereo Layers implementation.
	 * 
	 * Return a FTextureRHIRef pointing to a texture that can be composed on top of each eye without applying reprojection to it.
	 * Return nullptr to render head locked stereo layers into the same render target as other layer types, in which case InOutViewport must not be modified.
	 */
	virtual FTextureRHIRef GetOverlayLayerTarget_RenderThread(int32 ViewIndex, FIntRect& InOutViewport) { return nullptr; }

	/**
	 * Implement this method to override the render target for scene based stereo layers.
	 * Return nullptr to render stereo layers into the normal render target passed to the stereo layers scene view extension, in which case OutViewport must not be modified.
	 */
	virtual FTextureRHIRef GetSceneLayerTarget_RenderThread(int32 ViewIndex, FIntRect& InOutViewport) { return nullptr; }

	mutable TSharedPtr<class FDefaultStereoLayers, ESPMode::ThreadSafe> DefaultStereoLayers;
	
	friend class FDefaultStereoLayers;

	TUniquePtr<FDefaultSpectatorScreenController> SpectatorScreenController;

	// Sane pixel density values
	static constexpr float PixelDensityMin = 0.1f;
	static constexpr float PixelDensityMax = 2.0f;

	/**
	 * CVar sink for pixel density
	 */
	static UE_API void CVarSinkHandler();
	static UE_API FAutoConsoleVariableSink CVarSink;

private:
	bool bHeadTrackingEnforced;
};

#undef UE_API

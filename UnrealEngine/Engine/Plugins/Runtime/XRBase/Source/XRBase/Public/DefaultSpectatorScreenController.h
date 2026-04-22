// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "RendererInterface.h"
#include "ISpectatorScreenController.h"
#include "HeadMountedDisplayTypes.h"
#include "UObject/WeakObjectPtrTemplates.h"
#include "TextureResource.h"

#define UE_API XRBASE_API

class FHeadMountedDisplayBase;

UE_DEPRECATED(5.6, "Override AddSpectatorModePass instead")
DECLARE_DELEGATE_FiveParams(FSpectatorScreenRenderDelegate, FRHICommandListImmediate& /* RHICmdList */, FTextureRHIRef /* TargetTexture */, FTextureRHIRef /* EyeTexture */, FTextureRHIRef /* OtherTexture */, FVector2D /* WindowSize */);

/** 
 *	Default implementation of spectator screen controller.
 *
 */
class FDefaultSpectatorScreenController : public ISpectatorScreenController, public TSharedFromThis<FDefaultSpectatorScreenController, ESPMode::ThreadSafe>
{
public:
	UE_API FDefaultSpectatorScreenController(FHeadMountedDisplayBase* InHMDDevice);

	PRAGMA_DISABLE_DEPRECATION_WARNINGS // for deprecated fields
	virtual ~FDefaultSpectatorScreenController() {}
	FDefaultSpectatorScreenController(const FDefaultSpectatorScreenController&) = default;
	FDefaultSpectatorScreenController(FDefaultSpectatorScreenController&&) = default;
	FDefaultSpectatorScreenController& operator=(const FDefaultSpectatorScreenController&) = default;
	FDefaultSpectatorScreenController& operator=(FDefaultSpectatorScreenController&&) = default;
	UE_API PRAGMA_ENABLE_DEPRECATION_WARNINGS

	// ISpectatorScreenController
	virtual ESpectatorScreenMode GetSpectatorScreenMode() const override;
	UE_API virtual void SetSpectatorScreenMode(ESpectatorScreenMode Mode) override;
	UE_API virtual void SetSpectatorScreenTexture(UTexture* InTexture) override;
	UE_API virtual UTexture* GetSpectatorScreenTexture() const override;
	UE_API virtual void SetSpectatorScreenModeTexturePlusEyeLayout(const FSpectatorScreenModeTexturePlusEyeLayout& Layout) override;

	UE_DEPRECATED(5.6, "This API will be removed. Override RenderSpectatorScreen_RenderThread if you need to change this behavior.")
	UE_API PRAGMA_DISABLE_DEPRECATION_WARNINGS
	FSpectatorScreenRenderDelegate* GetSpectatorScreenRenderDelegate_RenderThread();
	PRAGMA_ENABLE_DEPRECATION_WARNINGS

	// Implementation methods called by HMD
	UE_DEPRECATED(5.6, "Use the FSceneViewFamily& overload instead.")
	virtual void BeginRenderViewFamily() {}
	UE_API virtual void BeginRenderViewFamily(FSceneViewFamily& ViewFamily);

	ESpectatorScreenMode GetSpectatorScreenMode_RenderThread()
	{
		return SpectatorScreenMode_RenderThread;
	}

	UE_DEPRECATED(5.6, "This API will be removed. Override AddSpectatorModePass if you need to change this behavior.")
	UE_API virtual void UpdateSpectatorScreenMode_RenderThread() final;
	UE_DEPRECATED(5.6, "Use the FRDGBuilder overload instead")
	UE_API virtual void RenderSpectatorScreen_RenderThread(FRHICommandListImmediate& RHICmdList, FRHITexture* BackBuffer, FTextureRHIRef SrcTexture, FVector2D WindowSize) final;
	UE_DEPRECATED(5.6, "Use the FRDGBuilder overload instead")
	UE_API virtual void RenderSpectatorScreen_RenderThread(FRHICommandListImmediate& RHICmdList, FRHITexture* BackBuffer, FTextureRHIRef SrcTexture, FTextureRHIRef LayersTexture, FVector2D WindowSize) final;
	UE_API virtual void RenderSpectatorScreen_RenderThread(class FRDGBuilder& GraphBuilder, FRDGTextureRef BackBuffer, FRDGTextureRef SrcTexture, FRDGTextureRef LayersTexture, FVector2f WindowSize);

protected:
	UE_DEPRECATED(5.6, "This function is no longer used and will be removed.")
	virtual void SetSpectatorScreenTextureRenderCommand(UTexture* SrcTexture) final {}
	UE_DEPRECATED(5.6, "This function is no longer used and will be removed.")
	virtual void SetSpectatorScreenTexture_RenderThread(FTextureRHIRef& InTexture) final {}

	UE_DEPRECATED(5.6, "This function is no longer used and will be removed.")
	virtual void SetSpectatorScreenModeTexturePlusEyeLayoutRenderCommand(const FSpectatorScreenModeTexturePlusEyeLayout& Layout) final {}
	UE_DEPRECATED(5.6, "This function is no longer used and will be removed.")
	virtual void SetSpectatorScreenModeTexturePlusEyeLayout_RenderThread(const FSpectatorScreenModeTexturePlusEyeLayout& Layout) final {}

	UE_API virtual FIntRect GetFullFlatEyeRect_RenderThread(const FRHITextureDesc& EyeTexture);
	UE_DEPRECATED(5.6, "Use the FRHITextureDesc overload instead")
	UE_API virtual FIntRect GetFullFlatEyeRect_RenderThread(FTextureRHIRef EyeTexture) final;

	UE_DEPRECATED(5.6, "This API will be removed. Override AddSpectatorModePass if you need to change this behavior.")
	UE_API virtual void RenderSpectatorModeSingleEyeLetterboxed(FRHICommandListImmediate& RHICmdList, FTextureRHIRef TargetTexture, FTextureRHIRef EyeTexture, FTextureRHIRef OtherTexture, FVector2D WindowSize) final;
	UE_DEPRECATED(5.6, "This API will be removed. Override AddSpectatorModePass if you need to change this behavior.")
	UE_API virtual void RenderSpectatorModeUndistorted(FRHICommandListImmediate& RHICmdList, FTextureRHIRef TargetTexture, FTextureRHIRef EyeTexture, FTextureRHIRef OtherTexture, FVector2D WindowSize) final;
	UE_DEPRECATED(5.6, "This API will be removed. Override AddSpectatorModePass if you need to change this behavior.")
	UE_API virtual void RenderSpectatorModeDistorted(FRHICommandListImmediate& RHICmdList, FTextureRHIRef TargetTexture, FTextureRHIRef EyeTexture, FTextureRHIRef OtherTexture, FVector2D WindowSize) final;
	UE_DEPRECATED(5.6, "This API will be removed. Override AddSpectatorModePass if you need to change this behavior.")
	UE_API virtual void RenderSpectatorModeSingleEye(FRHICommandListImmediate& RHICmdList, FTextureRHIRef TargetTexture, FTextureRHIRef EyeTexture, FTextureRHIRef OtherTexture, FVector2D WindowSize) final;
	UE_DEPRECATED(5.6, "This API will be removed. Override AddSpectatorModePass if you need to change this behavior.")
	UE_API virtual void RenderSpectatorModeTexture(FRHICommandListImmediate& RHICmdList, FTextureRHIRef TargetTexture, FTextureRHIRef EyeTexture, FTextureRHIRef OtherTexture, FVector2D WindowSize) final;
	UE_DEPRECATED(5.6, "This API will be removed. Override AddSpectatorModePass if you need to change this behavior.")
	UE_API virtual void RenderSpectatorModeMirrorAndTexture(FRHICommandListImmediate& RHICmdList, FTextureRHIRef TargetTexture, FTextureRHIRef EyeTexture, FTextureRHIRef OtherTexture, FVector2D WindowSize) final;
	UE_DEPRECATED(5.6, "This API will be removed. Override AddSpectatorModePass if you need to change this behavior.")
	UE_API virtual void RenderSpectatorModeSingleEyeCroppedToFill(FRHICommandListImmediate& RHICmdList, FTextureRHIRef TargetTexture, FTextureRHIRef EyeTexture, FTextureRHIRef OtherTexture, FVector2D WindowSize) final;

	UE_API virtual void AddSpectatorModePass(ESpectatorScreenMode SpectatorMode, class FRDGBuilder& GraphBuilder, FRDGTextureRef TargetTexture, FRDGTextureRef EyeTexture, FRDGTextureRef OtherTexture, FVector2f WindowSize);

	UE_DEPRECATED(5.6, "Use GetFallbackRDGTexture instead")
	UE_API virtual FRHITexture* GetFallbackRHITexture() const final;
	UE_API virtual FRDGTextureRef GetFallbackRDGTexture(FRDGBuilder& GraphBuilder) const;

	static constexpr ESpectatorScreenMode DefaultSpectatorMode = ESpectatorScreenMode::SingleEyeCroppedToFill;

	ESpectatorScreenMode SpectatorScreenMode_GameThread = DefaultSpectatorMode;
	TWeakObjectPtr<UTexture> SpectatorScreenTexture;

	ESpectatorScreenMode SpectatorScreenMode_RenderThread = DefaultSpectatorMode;
	FSpectatorScreenModeTexturePlusEyeLayout SpectatorScreenModeTexturePlusEyeLayout_RenderThread;
	FTextureResource* SpectatorScreenTexture_RenderThread = nullptr; 
	ERHIFeatureLevel::Type FeatureLevel_RenderThread;
	EShaderPlatform ShaderPlatform_RenderThread;

	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	UE_DEPRECATED(5.6, "This API will be removed. Override AddSpectatorModePass if you need to change this behavior.")
	FSpectatorScreenRenderDelegate SpectatorScreenDelegate_RenderThread;
	UE_DEPRECATED(5.6, "This API will be removed. Override AddSpectatorModePass if you need to change this behavior.")
	ESpectatorScreenMode DelegateSpectatorScreenMode_RenderThread = ESpectatorScreenMode::Disabled;
	UE_DEPRECATED(5.6, "This field will be removed. Check for IStereoLayers::LAYER_FLAG_DEBUG instead.")
	TArray<int32> DebugCanvasLayerIDs;
	PRAGMA_ENABLE_DEPRECATION_WARNINGS

	class Helpers
	{
	public:
		static UE_API FIntRect GetEyeCroppedToFitRect(FVector2D EyeCenterPoint, const FIntRect& EyeRect, const FIntRect& TargetRect);
		static UE_API FIntRect GetLetterboxedDestRect(const FIntRect& SrcRect, const FIntRect& TargetRect);
	};

private:
	UE_DEPRECATED(5.6, "Use the FRDGBuilder overload instead")
	UE_API void CopyEmulatedLayers(FRHICommandListImmediate& RHICmdList, FTextureRHIRef TargetTexture, const FIntRect SrcRect, const FIntRect DstRect);
	UE_API void CopyEmulatedLayers(FRDGBuilder& GraphBuilder, FRDGTextureRef TargetTexture, const FIntRect SrcRect, const FIntRect DstRect);

	UE_API void AddSpectatorModePassTexturePlusEye(FRDGBuilder& GraphBuilder, FRDGTextureRef TargetTexture, FRDGTextureRef EyeTexture, FRDGTextureRef OtherTexture);

	FHeadMountedDisplayBase* HMDDevice;
	// Face locked stereo layers are composited to a single texture which has to be copied over to the spectator screen.
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	UE_DEPRECATED(5.6, "Use StereoLayersTextureRDG instead")
	FTextureRHIRef StereoLayersTexture;
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
	FRDGTextureRef StereoLayersTextureRDG;
};

#undef UE_API

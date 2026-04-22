// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/AssetUserData.h"
#include "MediaCapture.h"

#include "MediaFrameworkWorldSettingsAssetUserData.generated.h"

enum EViewModeIndex : int;

class UMediaFrameworkWorldSettingsAssetUserData;
class UMediaOutput;
class UTextureRenderTarget2D;

/**
 * FMediaFrameworkCaptureCurrentViewportOutputInfo
 */
USTRUCT()
struct FMediaFrameworkCaptureCurrentViewportOutputInfo
{
	GENERATED_BODY()

	FMediaFrameworkCaptureCurrentViewportOutputInfo();

	UPROPERTY(EditAnywhere, Category="MediaViewportCapture")
	TObjectPtr<UMediaOutput> MediaOutput;

	UPROPERTY(EditAnywhere, Category="MediaViewportCapture")
	FMediaCaptureOptions CaptureOptions;

	UPROPERTY()
	TEnumAsByte<EViewModeIndex> ViewMode;
};

/**
 * FMediaFrameworkCaptureCameraViewportCameraOutputInfo
 */
USTRUCT()
struct FMediaFrameworkCaptureCameraViewportCameraOutputInfo
{
	GENERATED_BODY()

	FMediaFrameworkCaptureCameraViewportCameraOutputInfo();

	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	// Note: We need to explicitly disable warnings on these constructors/operators for clang to be happy with deprecated variables
	// this is a requirement for clang to compile without warnings.
	~FMediaFrameworkCaptureCameraViewportCameraOutputInfo() = default;
	FMediaFrameworkCaptureCameraViewportCameraOutputInfo(const FMediaFrameworkCaptureCameraViewportCameraOutputInfo&) = default;
	FMediaFrameworkCaptureCameraViewportCameraOutputInfo(FMediaFrameworkCaptureCameraViewportCameraOutputInfo&&) = default;
	FMediaFrameworkCaptureCameraViewportCameraOutputInfo& operator=(const FMediaFrameworkCaptureCameraViewportCameraOutputInfo&) = default;
	FMediaFrameworkCaptureCameraViewportCameraOutputInfo& operator=(FMediaFrameworkCaptureCameraViewportCameraOutputInfo&&) = default;
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
	
	UE_DEPRECATED(5.7, "Use Cameras instead")
	UPROPERTY(Meta = (DeprecatedProperty, DeprecationMessage = "This property is no longer supported. Use Cameras instead"))
	TArray<TLazyObjectPtr<AActor>> LockedActors_DEPRECATED;
	
	UPROPERTY(EditAnywhere, Category="MediaViewportCapture")
	TArray<TSoftObjectPtr<AActor>> Cameras;

	UPROPERTY(EditAnywhere, Category="MediaViewportCapture")
	TObjectPtr<UMediaOutput> MediaOutput;

	UPROPERTY(EditAnywhere, Category="MediaViewportCapture")
	FMediaCaptureOptions CaptureOptions;

	UPROPERTY()
	TEnumAsByte<EViewModeIndex> ViewMode;

	bool Serialize(FArchive& Ar);
	
#if WITH_EDITORONLY_DATA
	void PostSerialize(const FArchive& Ar);
#endif
	
private:
	//DEPRECATED 4.21 The type of LockedCameraActors has changed and will be removed from the code base in a future release. Use LockedActors.
	UPROPERTY()
	TArray<TObjectPtr<AActor>> LockedCameraActors_DEPRECATED;
	friend UMediaFrameworkWorldSettingsAssetUserData;
};

template<> struct TStructOpsTypeTraits<FMediaFrameworkCaptureCameraViewportCameraOutputInfo> : public TStructOpsTypeTraitsBase2<FMediaFrameworkCaptureCameraViewportCameraOutputInfo>
{
	enum
	{
		WithSerializer = true,

#if WITH_EDITORONLY_DATA
		WithPostSerialize = true,
#endif
	};
};

/**
 * FMediaFrameworkCaptureRenderTargetCameraOutputInfo
 */
USTRUCT()
struct FMediaFrameworkCaptureRenderTargetCameraOutputInfo
{
	GENERATED_BODY()

	FMediaFrameworkCaptureRenderTargetCameraOutputInfo();

	UPROPERTY(EditAnywhere, Category="MediaRenderTargetCapture")
	TObjectPtr<UTextureRenderTarget2D> RenderTarget;

	UPROPERTY(EditAnywhere, Category="MediaRenderTargetCapture")
	TObjectPtr<UMediaOutput> MediaOutput;

	UPROPERTY(EditAnywhere, Category="MediaRenderTargetCapture")
	FMediaCaptureOptions CaptureOptions;
};


/**
 * UMediaFrameworkCaptureCameraViewportAssetUserData
 */
UCLASS(MinimalAPI, config = Editor)
class UMediaFrameworkWorldSettingsAssetUserData : public UAssetUserData
{
	GENERATED_BODY()

public:
	UMediaFrameworkWorldSettingsAssetUserData();

	UPROPERTY(EditAnywhere, config, Category="Media Render Target Capture", meta=(ShowOnlyInnerProperties))
	TArray<FMediaFrameworkCaptureRenderTargetCameraOutputInfo> RenderTargetCaptures;

	UPROPERTY(EditAnywhere, config, Category="Media Viewport Capture", meta=(ShowOnlyInnerProperties))
	TArray<FMediaFrameworkCaptureCameraViewportCameraOutputInfo> ViewportCaptures;

	/**
	 * Capture the current viewport. It may be the level editor active viewport or a PIE instance launch with "New Editor Window PIE".
	 * @note The behavior is different from MediaCapture.CaptureActiveSceneViewport. Here we can capture the editor viewport (since we are in the editor).
	 * @note If the viewport is the level editor active viewport, then all inputs will be disabled and the viewport will always rendered.
	 */
	UPROPERTY(EditAnywhere, config, Category="Media Current Viewport Capture", meta=(DisplayName="Current Viewport"))
	FMediaFrameworkCaptureCurrentViewportOutputInfo CurrentViewportMediaOutput;

public:
	virtual void Serialize(FArchive& Ar) override;
};

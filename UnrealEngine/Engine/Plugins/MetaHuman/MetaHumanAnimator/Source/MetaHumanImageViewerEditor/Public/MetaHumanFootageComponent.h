// Copyright Epic Games, Inc.All Rights Reserved.

#pragma once

#include "Components/PrimitiveComponent.h"
#include "CameraCalibration.h"

#include "MetaHumanFootageComponent.generated.h"

#define UE_API METAHUMANIMAGEVIEWEREDITOR_API

enum class EABImageViewMode;

USTRUCT()
struct FFootagePlaneData
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category = "Footage Plane")
	TObjectPtr<class UStaticMeshComponent> FootagePlane;

	UPROPERTY(VisibleAnywhere, Category = "Footage Plane")
	TObjectPtr<class UTexture> ColorMediaTexture;

	UPROPERTY(VisibleAnywhere, Category = "Footage Plane")
	TObjectPtr<class UTexture> DepthMediaTexture;
};

/**
 * A component that handles displaying of footage data in an AB viewport.
 * Internally this component holds two plane static meshes with a material capable
 * of displaying color or depth data. The meshes are transformed to account
 * for the camera position and to be in the right aspect ratio.
 */
UCLASS(MinimalAPI)
class UMetaHumanFootageComponent
	: public UPrimitiveComponent
{
	GENERATED_BODY()

public:
	UE_API UMetaHumanFootageComponent();

	//~Begin UObject interface
	UE_API virtual void PostLoad() override;
	//~End UObject interface

	/** Position the plane meshes to be in the right location and scale based given a camera calibration */
	UE_API void SetCameraCalibration(class UCameraCalibration* InCameraCalibration);

	/** When no calibration is known, specify the footage resolution instead in order to position the plane */
	UE_API void SetFootageResolution(const FVector2D& InResolution);

	/** Sets the active camera to use in the calibration */
	UE_API void SetCamera(const FString& InCamera);

	/** Set the media textures that represent color and depth data. The visibility of each texture is controlled by ShowRGBChannel and ShowDepthChannel */
	UE_API void SetMediaTextures(class UTexture* InColorMediaTexture, class UTexture* InDepthMediaTexture, bool bNotifyMaterial = false);

	/** Sets the depth range used in the footage plane material */
	UE_API void SetDepthRange(int32 InDepthNear, int32 InDepthFar);

	/** Returns both plane components */
	UE_API TArray<class UStaticMeshComponent*> GetFootagePlaneComponents() const;

	/** Returns the plane component of the given AB view mode */
	UE_API class UStaticMeshComponent* GetFootagePlaneComponent(EABImageViewMode InViewMode) const;

	/** Set the footage plane visibility on the given AB view mode */
	UE_API void SetFootageVisible(EABImageViewMode InViewMode, bool bInIsVisible);

	/** Display the color channel in the given AB view mode */
	UE_API void ShowColorChannel(EABImageViewMode InViewMode);

	/** Set if we should display undistort the footage being displayed */
	UE_API void SetUndistortionEnabled(EABImageViewMode InViewMode, bool bUndistort);

	/**
	 * Calculates the field of view required to focus on this footage component
	 * also returns the screen rectangle this component occupies on screen a transform
	 * corresponding the extrinsic camera parameters that can be used to focus the viewport
	 * on the footage in 3D space
	 */
	UE_API void GetFootageScreenRect(const FVector2D& InViewportSize, float& OutFieldOfView, FBox2D& OutScreenRect, FTransform& OutCameraTransform) const;

private:

	/** Creates the material used to display color and depth data from footage in the viewport  */
	UE_API void CreateFootageMaterialInstances();

	/** Returns the material instance of a given AB view mode */
	UE_API class UMaterialInstanceDynamic* GetFootageMaterialInstance(EABImageViewMode InViewMode);

	/** Configure a given AB footage plane to display color*/
	UE_API void ConfigurePlane(EABImageViewMode InViewMode);

private:

	UPROPERTY(VisibleAnywhere, Category = "Footage Planes")
	TMap<EABImageViewMode, FFootagePlaneData> FootagePlanes;

	UPROPERTY(VisibleAnywhere, Category = "Calibration")
	TObjectPtr<UCameraCalibration> CameraCalibration;

	UPROPERTY(VisibleAnywhere, Category = "Calibration")
	FString Camera;

	// Depth range
	int32 DepthDataNear = 10;
	int32 DepthDataFar = 50;

	// An effective calibration to use when no actual calibration is known.
	// This sets the image resolution from SetFootageResolution and an
	// arbitrary field of view. Its sufficient to place the
	// image plane in the scene. 
	FCameraCalibration EffectiveCalibration;
};

#undef UE_API

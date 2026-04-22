// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ProceduralMeshComponent.h"

#include "MetaHumanDepthMeshComponent.generated.h"

#define UE_API METAHUMANIMAGEVIEWEREDITOR_API

UCLASS(MinimalAPI)
class UMetaHumanDepthMeshComponent
	: public UProceduralMeshComponent
{
	GENERATED_BODY()
public:

	UE_API UMetaHumanDepthMeshComponent(const FObjectInitializer& InObjectInitializer);

	//~ Begin UActorComponent Interface.
	UE_API void OnRegister() override;
	UE_API void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	//~ End UActorComponent Interface.

	//~ Begin USceneComponent Interface.
	UE_API virtual FBoxSphereBounds CalcBounds(const FTransform& InLocalToWorld) const override;
	//~ End USceneComponent Interface.

	/** Sets the texture with depth data to display the mesh */
	UE_API void SetDepthTexture(class UTexture* InDepthTexture);

	/** Sets the camera calibration to calculate the placement of the depth mesh on the viewport */
	UE_API void SetCameraCalibration(class UCameraCalibration* InCameraCalibration);

	/** Set the depth near and far planes to clamp the display of depth data */
	UE_API void SetDepthRange(float InDepthNear, float InDepthFar);

	/** Set the resolution of the depth mesh  */
	UE_API void SetSize(int32 InWidth, int32 InHeight);

private:
	/** Sets depth plane transform based on the depth far plane */
	UE_API void SetDepthPlaneTransform(bool bInNotifyMaterial = false);

	UE_API void UpdateMaterialDepth();
	UE_API void UpdateMaterialTexture();
	UE_API void UpdateMaterialCameraIntrinsics();

private:

	UPROPERTY()
	TObjectPtr<UCameraCalibration> CameraCalibration;

	UPROPERTY(EditAnywhere, Category = Texture)
	TObjectPtr<UTexture> DepthTexture;

	UPROPERTY()
	int32 Width = -1;

	UPROPERTY()
	int32 Height = -1;

	UPROPERTY(EditAnywhere, Category = DepthRange)
	float DepthNear = 10.0f;

	UPROPERTY(EditAnywhere, Category = DepthRange)
	float DepthFar = 55.5f;
};

#undef UE_API

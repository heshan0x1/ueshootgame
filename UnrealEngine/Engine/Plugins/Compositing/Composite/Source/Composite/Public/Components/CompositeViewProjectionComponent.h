// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Components/SceneComponent.h"

#include "CompositeViewProjectionComponent.generated.h"

#define UE_API COMPOSITE_API

class UCameraComponent;
class UMaterialParameterCollection;
class UWorld;

/** Component responsible for continuously updating the specified material parameter collection with a camera view projection matrix (to be used for texture projection in materials). */
UCLASS(MinimalAPI, EditInlineNew, HideCategories = (Activation, Transform, Lighting, Rendering, Tags, Cooking, Physics, LOD, AssetUserData, Navigation), ClassGroup = Composite, meta = (BlueprintSpawnableComponent, DisplayName = "Camera View Projection"))
class UCompositeViewProjectionComponent : public UActorComponent
{
	GENERATED_UCLASS_BODY()

public:
	//~ Begin UObject interface
	UE_API virtual void PostInitProperties() override;
	UE_API virtual void PostDuplicate(bool bDuplicateForPIE) override;
	UE_API virtual void PostEditImport() override;
	UE_API virtual void PostLoad() override;
	//~ End UObject interface

	//~ Begin UActorComponent interface
	UE_API virtual void OnRegister() override;
	UE_API virtual void OnUnregister() override;
	UE_API virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	//~ End UActorComponent interface

#if WITH_EDITOR
	UE_API virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif //WITH_EDITOR

public:
	/** Whether or not the component activates the view-projection matrix Material Parameter Collection update. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category = "CameraProjection")
	bool bIsEnabled = true;

	/** The Material Parameter Collection in which the view-projection matrix should be stored. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category = "CameraProjection")
	TObjectPtr<UMaterialParameterCollection> MaterialParameterCollection;

	/** Parameter name of the first element of the transform in the Material Parameter Collection set above.  Requires space for 4 vectors. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category = "CameraProjection")
	FName MatrixParameterName;

public:
	/** Force update the Material Parameter Collection this frame. */
	UFUNCTION(BlueprintCallable, Category = "CameraProjection")
	UE_API void ForceUpdate();

	/** Target component getter. */
	UFUNCTION(BlueprintGetter)
	UE_API FComponentReference& GetTargetComponent();

	/** Target component setter. */
	UFUNCTION(BlueprintSetter)
	UE_API void SetTargetComponent(const FComponentReference& InComponentReference);

private:
	/** The CineCameraComponent whose view-projection matrix is used. */
	UPROPERTY(EditInstanceOnly, BlueprintGetter = GetTargetComponent, BlueprintSetter = SetTargetComponent, Category = "CameraProjection", meta = (AllowPrivateAccess, UseComponentPicker, AllowedClasses = "/Script/Engine.CameraComponent, /Script/CinematicCamera.CineCameraComponent"))
	FComponentReference TargetCameraComponent;

	/** If TargetCameraComponent is not set, initialize it to the first CineCameraComponent on the same actor as this component */
	void InitDefaultCamera();
	
	/** Update the Material Parameter Collection with the camera view transform information. */
	void UpdateProjection(UWorld* InWorld) const;

	/** Cached most recent target camera, used to clean up the old camera when the user changes the target */
	UPROPERTY()
	TObjectPtr<UCameraComponent> LastCameraComponent = nullptr;

	/** Cached view projection matrix, to avoid needless MPC updates. */
	mutable FMatrix LastViewProjectionMatrix = FMatrix::Identity;

	/** Post-viewport update callback. */
	FDelegateHandle OnWorldPreSendAllEndOfFrameUpdatesHandle;
};

#undef UE_API


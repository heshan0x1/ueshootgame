// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/StaticMeshComponent.h"
#include "IIdentifiableXRDevice.h" // for FXRDeviceId
#include "UObject/ObjectMacros.h"
#include "XRDeviceVisualizationComponent.generated.h"

#define UE_API XRBASE_API

class UMaterialInterface;
class UMotionControllerComponent;
class UStaticMesh;

UCLASS(MinimalAPI, Blueprintable, meta = (BlueprintSpawnableComponent), ClassGroup = MotionController)
class UXRDeviceVisualizationComponent : public UStaticMeshComponent
{
	GENERATED_UCLASS_BODY()

	/** Whether the visualization offered by this component is being used. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetIsVisualizationActive, Category = "Visualization")
	bool bIsVisualizationActive;

	UFUNCTION(BlueprintSetter)
	UE_API void SetIsVisualizationActive(bool bNewVisualizationState);

	/** Determines the source of the desired model. By default, the active XR system(s) will be queried and (if available) will provide a model for the associated device. NOTE: this may fail if there's no default model; use 'Custom' to specify your own. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetDisplayModelSource, Category = "Visualization")
	FName DisplayModelSource; 

	static UE_API FName CustomModelSourceId;
	UFUNCTION(BlueprintSetter)
	UE_API void SetDisplayModelSource(const FName NewDisplayModelSource); 

	/** A mesh override that'll be displayed attached to this MotionController. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetCustomDisplayMesh, Category = "Visualization")
	TObjectPtr<UStaticMesh> CustomDisplayMesh; 

	UFUNCTION(BlueprintSetter)
	UE_API void SetCustomDisplayMesh(UStaticMesh* NewDisplayMesh); 

	/** Material overrides for the specified display mesh. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visualization")
	TArray<TObjectPtr<UMaterialInterface>> DisplayMeshMaterialOverrides; 

	/** Callback for asynchronous display model loads (to set materials, etc.) */
	UE_API void OnDisplayModelLoaded(UPrimitiveComponent* DisplayComponent); 

	/** Set by the parent MotionController once tracking kicks in. */
	bool bIsRenderingActive;

	/** Whether this component can be displayed, depending on whether the parent MotionController has activated its rendering. */
	UE_API bool CanDeviceBeDisplayed();

	FName MotionSource;

	enum class EModelLoadStatus : uint8
	{
		Unloaded,
		Pending,
		InProgress,
		Complete
	};
	EModelLoadStatus DisplayModelLoadState = EModelLoadStatus::Unloaded;

	FXRDeviceId DisplayDeviceId;

	UE_API void RefreshMesh();
	UE_API UMotionControllerComponent* FindParentMotionController();
	UE_API void SetMaterials(int32 MatCount);
	UE_API void OnRegister() override;
	UE_API void OnUnregister() override;
#if WITH_EDITOR
	UE_API void OnCloseVREditor();
#endif

public:

	UFUNCTION(BlueprintSetter, Category="MotionController")
	UE_API void SetIsRenderingActive(bool bRenderingIsActive);

private:
	UE_API void OnInteractionProfileChanged();

	bool bInteractionProfilePresent = false;
};

#undef UE_API

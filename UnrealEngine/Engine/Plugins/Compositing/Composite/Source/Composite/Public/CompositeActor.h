// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "GameFramework/Actor.h"

#include "Passes/CompositePassBase.h"
#include "Layers/CompositeLayerBase.h"

#include "CompositeActor.generated.h"

#define UE_API COMPOSITE_API

class ACameraActor;
class UCompositeCoreSubsystem;
class UCompositeShadowReflectionCatcherComponent;
class UPrimitiveComponent;
class UCompositeSceneCapture2DComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnCompositeActorPostJoinMultiUserSession);

USTRUCT()
struct FSceneCaptureComponentArray
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<TObjectPtr<UCompositeSceneCapture2DComponent>> Components;
};

/* Main render color space & encoding output mode. */
UENUM(BlueprintType)
enum class ECompositeMainRenderOutputMode : uint8
{
	Default UMETA(DisplayName = "Default", ToolTip = "Default output from post-processing's tonemap, usually sRGB display output with tone curve, equivalent to Final Color (LDR) in RGB."),
	FinalColorHDR UMETA(DisplayName = "HDR Linear", ToolTip = "Linear output from post-processing's tonemap, equivalent to Final Color (HDR) in Linear Working Color Space."),
	FinalToneCurveHDR UMETA(DisplayName = "HDR Linear with Tone Curve", ToolTip = "Linear with tone curve output from post-processing's tonemap, equivalent to Final Color (with tone curve) in Linear sRGB gamut."),
};

/**
* Constrain composite render to certain view modes for multi-viewport workflows where unlit or wireframe viewports will not conflict with the composite.
*/
UENUM(BlueprintType)
enum class ECompositeAllowedViewModes : uint8
{
	Default UMETA(ToolTip = "Compositing is allowed on viewports with Lit, Path Tracing or Unknown view modes."),
	MediaProfileUnknown UMETA(DisplayName = "Media Profile (Unknown)", ToolTip = "Compositing is only allowed with the Unknown view mode, the default when media profile does its own rendering."),
	AllViewModes UMETA(ToolTip = "Compositing is allowed with all view modes."),
};

/** Actor used to control properties of the composite pipeline. */
UCLASS(MinimalAPI)
class ACompositeActor : public AActor
{
	GENERATED_BODY()

public:
	/** Constructor. */
	UE_API ACompositeActor(const FObjectInitializer& ObjectInitializer);

	/** Destructor. */
	UE_API ~ACompositeActor();

	//~ Begin UObject interface
	UE_API virtual void PostLoad() override;
	//~ End UObject interface

	//~ Begin AActor Interface
	UE_API virtual void PostRegisterAllComponents() override;
	UE_API virtual void UnregisterAllComponents(bool bForReregister = false) override;
	UE_API virtual void PostUnregisterAllComponents() override;
	UE_API virtual void Tick(float DeltaSeconds) override;
	UE_API virtual void Destroyed() override;
	//~ End AActor Interface

#if WITH_EDITOR
	UE_API virtual void PreDuplicateFromRoot(FObjectDuplicationParameters& DupParams) override;
	UE_API virtual void PreEditChange(FProperty* PropertyAboutToChange) override;
	UE_API virtual void PostEditChangeChainProperty(struct FPropertyChangedChainEvent& PropertyChangedChainEvent) override;
	UE_API virtual void PreEditUndo() override;
	UE_API virtual void PostEditUndo() override;
	UE_API virtual void PostTransacted(const FTransactionObjectEvent& TransactionEvent) override;
	UE_API virtual void SetIsTemporarilyHiddenInEditor(bool bIsHidden) override;
	UE_API virtual bool ShouldTickIfViewportsOnly() const override;
#endif // WITH_EDITOR

	/** Get the (locally) active state. */
	UFUNCTION(BlueprintGetter)
	UE_API bool IsActive() const;

	/** Set the (locally) active state. */
	UFUNCTION(BlueprintSetter)
	UE_API void SetActive(bool bInActive);

	/** Get the enabled state. */
	UFUNCTION(BlueprintGetter)
	UE_API bool IsEnabled() const;

	/** Set the enabled state. */
	UFUNCTION(BlueprintSetter)
	UE_API void SetEnabled(bool bInEnabled);

	/** Camera component getter. */
	UFUNCTION(BlueprintGetter)
	UE_API FComponentReference& GetCamera();

	/** Camera component setter. */
	UFUNCTION(BlueprintSetter)
	UE_API void SetCamera(const FComponentReference& InComponentReference);

	/** Get the composite layers. */
	UFUNCTION(BlueprintGetter)
	UE_API TArray<UCompositeLayerBase*> GetCompositeLayers();

	/** Get the composite layers. */
	UE_API const TArray<TObjectPtr<UCompositeLayerBase>>& GetCompositeLayers() const;

	/** Set the composite layers. */
	UFUNCTION(BlueprintSetter)
	UE_API void SetCompositeLayers(TArray<UCompositeLayerBase*> InLayers);

private:
	/** Component responsible for continuously updating a material parameter collection with the composite camera view projection matrix. */
	UPROPERTY(Instanced)
	TObjectPtr<class UCompositeViewProjectionComponent> ViewProjectionComponent;

	/** Whether or not the composite behavior is active locally - used primarily for multi-user. */
	UPROPERTY(NonTransactional, EditInstanceOnly, BlueprintGetter = IsActive, BlueprintSetter = SetActive, Category = "Composite", meta = (AllowPrivateAccess = true))
	bool bIsActive;

	/** Whether or not the composite behavior is enabled. */
	UPROPERTY(EditAnywhere, BlueprintGetter = IsEnabled, BlueprintSetter = SetEnabled, Interp, Category = "Composite", meta = (DisplayName = "Enabled", AllowPrivateAccess = true, DisplayPriority = "0"))
	bool bIsEnabled;

public:
	/** Composite render (output) resolution. Currently only used for transient scene capture render targets. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category = "Composite")
	FIntPoint RenderResolution;

private:
	/** The primary camera used for the composite. Used by the camera view projection component for continuous view-projection matrix updates. */
	UPROPERTY(EditInstanceOnly, BlueprintGetter = GetCamera, BlueprintSetter = SetCamera, Category = "Composite", meta = (AllowPrivateAccess, UseComponentPicker, AllowAnyActor, AllowedClasses = "/Script/Engine.CameraComponent, /Script/CinematicCamera.CineCameraComponent"))
	FComponentReference Camera;

	/** Array of composite layers for merging or processing images. */
	UPROPERTY(EditAnywhere, BlueprintGetter = GetCompositeLayers, BlueprintSetter = SetCompositeLayers, Instanced, Category = "Composite", meta = (DisplayName = "Layers"))
	TArray<TObjectPtr<UCompositeLayerBase>> CompositeLayers;
public:

	/** Also provide the post-processing composite graph to SSR input. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Composite", AdvancedDisplay)
	bool bEnableScreenSpaceReflections;

	/** Override default view user flags. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Composite", AdvancedDisplay)
	bool bOverridesViewUserFlags;

	/** Custom user flags value used to alter materials in the composite render pass. Set to one by default such that branching can be used in Lit materials. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category = "Composite", AdvancedDisplay, meta = (EditCondition = "bOverridesViewUserFlags"))
	int32 ViewUserFlags;

	/** Main render color space & encoding output mode. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Composite", AdvancedDisplay)
	ECompositeMainRenderOutputMode MainRenderOutput;

	/** Constrain composite rendering to specific view modes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Composite", AdvancedDisplay)
	ECompositeAllowedViewModes AllowedViewModes;

	/** Delegate called after joining a multi-user session. Can be used to automatically re-active the actor locally. */
	UPROPERTY(BlueprintAssignable, BlueprintCallable, BlueprintReadWrite, Category = "Composite")
	FOnCompositeActorPostJoinMultiUserSession OnPostJoinMultiUserSession;

public:
	/** Convenience function used by layers to request a scene capture component. */
	template<class RetType>
	UE_API RetType* FindOrCreateSceneCapture(const UCompositeLayerBase* InLayer, int32 InIndex = 0, FName InBaseName = NAME_None);

	/** Convenience function used by layers to remove and scene capture components. */
	UE_API void DestroySceneCaptures(const UCompositeLayerBase* InLayer);

	/** Returns true if the composite is actively rendering. */
	UE_API bool IsRendering() const;

	/** Called after we join a multi-user session. */
	UE_API void PostJoinConcertSession();

private:
	/**
	* Propagate rendering state changes to layers and the composite core subsystem.
	* 
	* @param bApply - Apply or remove external rendering state.
	*/
	void PropagateStateChange(bool bApply);

	/** Layer-managed scene capture components. */
	UPROPERTY()
	TMap<TWeakObjectPtr<const UCompositeLayerBase>, FSceneCaptureComponentArray> SceneCapturesPerLayer;

#if WITH_EDITORONLY_DATA
	/** Used to update components after we remove layers. */
	TArray<UCompositeLayerBase*> PreEditCompositeLayers;
#endif

	/** Flag used to determine in which cases we should ignore PostRegister / Unregister calls. */
	bool bIsModifyingAProperty;

	/** Befriend the actor's editor customizations. */
	friend class SCompositePanelLayerTree;
	friend class FCompositeActorCustomization;
	friend class FCompositeActorPanelDetailCustomization;
	friend class UCompositeActorFactory;
};

#undef UE_API


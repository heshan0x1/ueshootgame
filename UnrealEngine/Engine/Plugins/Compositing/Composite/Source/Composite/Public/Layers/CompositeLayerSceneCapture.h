// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CompositeLayerBase.h"

#include "CompositeLayerSceneCapture.generated.h"

#define UE_API COMPOSITE_API

class AActor;
class UPrimitiveComponent;
class FEditPropertyChain;

/**
* Layer for scene capture (or custom render pass) render integration.
*/
UCLASS(MinimalAPI, BlueprintType, Blueprintable, EditInlineNew, CollapseCategories, meta = (DisplayName = "Scene Capture"))
class UCompositeLayerSceneCapture : public UCompositeLayerBase
{
	GENERATED_BODY()

public:
	/** Constructor. */
	UE_API UCompositeLayerSceneCapture(const FObjectInitializer& ObjectInitializer);
	
	/** Destructor. */
	UE_API virtual ~UCompositeLayerSceneCapture();
	
	UE_API virtual void OnRemoved(const UWorld* World) override;

	//~ Begin UObject Interface
	UE_API virtual void BeginDestroy() override;

#if WITH_EDITOR
	UE_API virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	UE_API virtual void PreEditUndo() override;
	UE_API virtual void PostEditUndo() override;
#endif
	//~ End UObject Interface

	/** Getter function to override, returning pass layer proxies to be passed to the render thread. (Proxy objects should be allocated from the provided allocator.) */
	UE_API virtual bool GetProxy(FTraversalContext& InContext, FSceneRenderingBulkObjectAllocator& InFrameAllocator, FCompositeCorePassProxy*& OutProxy) const override;

	/** Get reflection & shadow caster actors. */
	UFUNCTION(BlueprintGetter)
	UE_API const TArray<TSoftObjectPtr<AActor>> GetActors() const;

	/** Set reflection & shadow caster actors. */
	UFUNCTION(BlueprintSetter)
	UE_API void SetActors(TArray<TSoftObjectPtr<AActor>> InActors);

	/** Get whether the scene capture is rendered as a custom render pass. */
	UFUNCTION(BlueprintGetter)
	UE_API bool IsCustomRenderPass() const;

	/** Set whether the scene capture is rendered as a custom render pass. */
	UFUNCTION(BlueprintSetter)
	UE_API void SetCustomRenderPass(bool bInIsFastRenderPass);

	/** Get whether registered meshes are marked as visible in scene capture only. */
	UFUNCTION(BlueprintGetter)
	UE_API bool IsVisibleInSceneCaptureOnly() const;

	/** Set whether registered meshes are marked as visible in scene capture only. */
	UFUNCTION(BlueprintSetter)
	UE_API void SetVisibleInSceneCaptureOnly(bool bInVisible);

	/** Sub-passes applied during post-processing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Instanced, Category = "", meta = (DisallowedClasses = "/Script/Composite.CompositePassDistortion", DisplayAfter = "bCustomRenderPass"))
	TArray<TObjectPtr<UCompositePassBase>> LayerPasses;

private:
	/** Convenience function to access the actor-managed scene capture component. */
	class UCompositeSceneCapture2DComponent* GetSceneCaptureComponent() const;

	/** Update scene capture visibility on registered mesh actors primitives. */
	void UpdatePrimitiveVisibilityState();
	
	/** Restore scene capture visibility on registered mesh actors primitives. */
	void RestorePrimitiveVisibilityState();

	/** Update scene capture show only components. */
	void UpdateSceneCaptureComponents(USceneCaptureComponent2D& SceneCaptureComponent) const;

	/** 5.7 hotfix: End of frame callback. */
	void OnEndOfFrameUpdate(UWorld* InWorld);

#if WITH_EDITOR
	/** 5.7 hotfix: Replacement for PreEditChange which can't be override in a hotfix. */
	void OnObjectPreEditChange(UObject* Object, const FEditPropertyChain& EditPropertyChain);
#endif

	/**
	 * List of actors to be rendered from the scene capture.
	 * 
	 * Automatically sets the scene capture's ShowOnlyComponents when updated.
	*/
	UPROPERTY(EditAnywhere, BlueprintGetter = GetActors, BlueprintSetter = SetActors, Category = "", meta = (AllowPrivateAccess = true))
	TArray<TSoftObjectPtr<AActor>> Actors;

#if WITH_EDITORONLY_DATA
	UE_DEPRECATED(5.7, "CachedVisibilityInSceneCapture has been deprecated in the 5.7.2 hotfix.")
	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "CachedVisibilityInSceneCapture has been deprecated in the 5.7.2 hotfix."))
	TMap<TWeakObjectPtr<UPrimitiveComponent>, bool> CachedVisibilityInSceneCapture;
#endif

	/** Visibility setting applied to registered actors. By default, registered meshes will only be visible in scene captures and no longer in the main render. */
	UPROPERTY(EditAnywhere, BlueprintGetter = IsVisibleInSceneCaptureOnly, BlueprintSetter = SetVisibleInSceneCaptureOnly, Category = "", meta = (AllowPrivateAccess = true))
	bool bVisibleInSceneCaptureOnly;

	/**
	 * Convenience toggle to render the scene capture as a fast/minimal Custom Render Pass, inlined in the main render & without support for lighting.
	 * 
	 * Automatically sets the scene capture's CaptureSource, bRenderInMainRenderer & bIgnoreScreenPercentage when updated.
	 */
	UPROPERTY(EditAnywhere, BlueprintGetter = IsCustomRenderPass, BlueprintSetter = SetCustomRenderPass, Category = "", meta = (AllowPrivateAccess = true))
	bool bCustomRenderPass;

	friend class FCompositeLayerSceneCaptureCustomization;
};

#undef UE_API


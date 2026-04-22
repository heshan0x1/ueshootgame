// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CompositeLayerBase.h"
#include "Passes/CompositePassOpenColorIO.h"
#include "Tickable.h"

#include "CompositeLayerPlate.generated.h"

#define UE_API COMPOSITE_API

class UPrimitiveComponent;
class UTexture;
class FObjectPreSaveContext;
class UTextureRenderTarget2D;

/** Plate sampling & rendering mode. */
UENUM(BlueprintType)
enum class ECompositePlateMode : uint8
{
	Texture = 0 UMETA(ToolTip = "Sample the texture directly in 2D screen space."),
	CompositeMesh = 1 UMETA(ToolTip = "Sample the composite meshes rendered in a built-in custom render pass (default). Automatic fallback to Texture."),
};

/**
* Convenience layer for media texture integration, with additional processing options.
* 
* With composite meshes and default plugin materials, the media texture is projected into the scene & rendered separately as a custom render pass.
* The result is dilated and combined with other layers such as the main render (where the same composite meshes are rendered as holdouts).
*/
UCLASS(MinimalAPI, BlueprintType, Blueprintable, EditInlineNew, CollapseCategories, meta = (DisplayName = "Plate"))
class UCompositeLayerPlate : public UCompositeLayerBase, public FTickableGameObject
{
	GENERATED_BODY()

public:
	/** Composite (media) texture material parameter name. */
	UE_API static const FLazyName CompositeTextureName;

	/** Constructor. */
	UE_API UCompositeLayerPlate(const FObjectInitializer& ObjectInitializer);
	
	/** Destructor. */
	UE_API virtual ~UCompositeLayerPlate();

	UE_API virtual void OnRemoved(const UWorld* World) override;

	UE_API virtual void OnRenderingStateChange(bool bApply) override;

	/** Set the enabled state. */
	UE_API virtual void SetEnabled(bool bInEnabled) override;

	//~ Begin FTickableGameObject interface
	UE_API virtual void Tick(float DeltaTime) override;
	
	UE_API virtual bool IsTickableWhenPaused() const override { return true; }
	UE_API virtual bool IsTickableInEditor() const override { return true; }
	UE_API virtual ETickableTickType GetTickableTickType() const override;
	UE_API virtual bool IsTickable() const override;

	UE_API virtual UWorld* GetTickableGameObjectWorld() const override;
	UE_API virtual TStatId GetStatId() const override;
	//~ End FTickableGameObject interface

	/** Getter function to override, returning pass layer proxies to be passed to the render thread. (Proxy objects should be allocated from the provided allocator.) */
	UE_API virtual bool GetProxy(FTraversalContext& InContext, FSceneRenderingBulkObjectAllocator& InFrameAllocator, FCompositeCorePassProxy*& OutProxy) const override;

	/** Get array of all composite mesh primitives. */
	UE_API TArray<UPrimitiveComponent*> GetPrimitives() const;

public:
	//~ Begin UObject Interface
	UE_API virtual void PostLoad() override;
	UE_API virtual void BeginDestroy() override;

#if WITH_EDITOR
	UE_API virtual bool CanEditChange(const FProperty* InProperty) const override;
	UE_API virtual void PreEditChange(FProperty* PropertyThatWillChange) override;
	UE_API virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	UE_API virtual void PostTransacted(const FTransactionObjectEvent& InTransactionEvent) override;
#endif
	//~ End UObject Interface

public:

	/** Get composite meshes. */
	UFUNCTION(BlueprintGetter)
	UE_API const TArray<TSoftObjectPtr<AActor>> GetCompositeMeshes() const;

	/** Set composite meshes. */
	UFUNCTION(BlueprintSetter)
	UE_API void SetCompositeMeshes(TArray<TSoftObjectPtr<AActor>> InCompositeMeshes);

	/** Get the plate sampling & rendering mode */
	UFUNCTION(BlueprintGetter)
	UE_API ECompositePlateMode GetPlateMode() const { return PlateMode; }

	/** Set the plate sampling & rendering mode */
	UFUNCTION(BlueprintSetter)
	UE_API void SetPlateMode(ECompositePlateMode InPlateMode);

	/** Returns the media texture, optionally pre-processed with layer & scene-only passes into a render target. */
	UFUNCTION(BlueprintCallable, Category = "Composite")
	UTexture* GetCompositeTexture() const;

private:
	/** Actors whose primitive mesh components will be marked as primitive alpha holdout, and rendered separately with the projected (media) texture. */
	UPROPERTY(EditAnywhere, BlueprintGetter = GetCompositeMeshes, BlueprintSetter = SetCompositeMeshes, Category = "", meta = (AllowPrivateAccess = true))
	TArray<TSoftObjectPtr<AActor>> CompositeMeshes;

public:
	/** Media texture for compositing layer. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "", meta = (AllowedClasses="/Script/Engine.Texture2D, /Script/MediaAssets.MediaTexture"))
	TObjectPtr<UTexture> Texture;

	/**
	* Media texture pre-processing passes, applied before rendering.
	* 
	* The texture is replaced with an internal render target.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Instanced, Category = "", meta = (DisallowedClasses = "/Script/Composite.CompositePassMaterial, /Script/Composite.CompositePassDistortion, /Script/Composite.CompositePassFXAA, /Script/Composite.CompositePassSMAA"))
	TArray<TObjectPtr<UCompositePassBase>> MediaPasses;

	/**
	* Passes applied after media passes, onto composite meshes only.
	* 
	* The texture is replaced with an internal render target.
	* 
	* NOTE: Set the plate "Mode" to "Texture" in order to skip these scene passes during final compositing in post-processing.
	* We now have an automatic scene pass to undistort a plate projected onto composite meshes. Using Texture mode can therefore
	* avoid the automatic undistort -> distort pipeline.
	*/
	UPROPERTY(BlueprintReadWrite, Instanced, Category = "", meta = (AllowedClasses = "/Script/Composite.CompositePassDistortion"))
	TArray<TObjectPtr<UCompositePassBase>> ScenePasses;

	/** Passes applied during final compositing in post-processing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Instanced, Category = "", meta = (DisallowedClasses = "/Script/Composite.CompositePassDistortion"))
	TArray<TObjectPtr<UCompositePassBase>> LayerPasses;

private:
	/** The plate sampling & rendering mode: direct texture sampling or separate composite mesh render. */
	UPROPERTY(EditAnywhere, BlueprintGetter = GetPlateMode, BlueprintSetter = SetPlateMode, Category = "", AdvancedDisplay, meta = (AllowPrivateAccess = true, DisplayName = "Mode"))
	ECompositePlateMode PlateMode;

private:
	/** Update the (media) texture on all composite meshes. We also add a user asset data to track changes. */
	void UpdateCompositeMeshes(bool bRegisterOnly = false) const;
	
	/** Update the (media) texture on a primitive component. */
	void UpdatePrimitiveComponent(UPrimitiveComponent& InPrimitiveComponent) const;

	/**
	* Apply (or remove) state to activate (or deactivate) the built-in custom render pass for composite meshes.
	* 
	* @param bApply Apply or remove external rendering state.
	* @param SourceWorld Optional source world to find the composite subsystem
	*/
	void PropagateStateChange(bool bApply, const UWorld* SourceWorld) const;

	/** Convenience function verify if the composite mesh actor is already in use by another layer. */
	bool IsCompositeMeshActorAlreadyInUse(TSoftObjectPtr<AActor> InCompositeMeshActor) const;

	/** Convenience function to get or create the specified render target at composite actor resolution. */
	TObjectPtr<UTextureRenderTarget2D> GetOrCreateRenderTarget(TObjectPtr<UTextureRenderTarget2D>& InRenderTarget) const;

	/** Get number of valid primitives. */
	int32 GetValidPrimitivesNum() const;

	/** Get number of valid passes. */
	int32 GetValidPassesNum(TArrayView<const TObjectPtr<UCompositePassBase>> InPasses) const;

	/** If Texture is a media profile media texture, attempt to open the associated media source */
	void TryOpenMediaProfileSource();
	
	/** Attempt to close any media profile sources being played for this plate */
	void TryCloseMediaProfileSource();
	
	/** Returns true if the plate layer is actively rendering. */
	bool IsRendering() const;
	
private:
	/** Convenience function to find the last valid pass. */
	int32 FindLastValidPassIndex(TArrayView<const TObjectPtr<UCompositePassBase>> InPasses) const;

	/** Convenience function to register pre-processing child passes. */
	UE::CompositeCore::ResourceId AddPreprocessingPasses(
		FTraversalContext& InContext,
		FSceneRenderingBulkObjectAllocator& InFrameAllocator,
		TArrayView<const TObjectPtr<UCompositePassBase>> InPasses,
		UE::CompositeCore::ResourceId TextureId,
		UE::CompositeCore::ResourceId OriginalTextureId,
		TFunction<TObjectPtr<UTextureRenderTarget2D>()> GetRenderTargetFn
	) const;

#if WITH_EDITOR
	/** Pre-edit list of registered composite meshes. */
	TArray<TSoftObjectPtr<AActor>> PreEditCompositeMeshes;

	/**
	 * Called before a level saves.
	 */
	void OnPreSaveWorld(UWorld* InWorld, FObjectPreSaveContext ObjectSaveContext);

	/**
	 * Called after a level has saved.
	 */
	void OnPostSaveWorld(UWorld* InWorld, FObjectPostSaveContext ObjectSaveContext);
#endif

	/** Processed media texture render target.*/
	UPROPERTY(Transient, NonTransactional)
	mutable TObjectPtr<UTextureRenderTarget2D> MediaRenderTarget;

	/** Processed media texture with additional passes only for scene composite meshes.*/
	UPROPERTY(Transient, NonTransactional)
	mutable TObjectPtr<UTextureRenderTarget2D> SceneRenderTarget;

	/** Cached number of valid media passes. */
	int32 CachedValidMediaPasses = 0;

	/** Cached number of valid scene passes. */
	int32 CachedValidScenePasses = 0;

	friend class FCompositeLayerPlateCustomization;
};

#undef UE_API


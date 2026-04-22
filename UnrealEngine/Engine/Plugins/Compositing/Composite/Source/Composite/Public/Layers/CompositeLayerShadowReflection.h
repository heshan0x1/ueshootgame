// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CompositeLayerBase.h"

#include "Engine/TextureRenderTarget2D.h"
#include "Containers/ContainersFwd.h"

#include "CompositeLayerShadowReflection.generated.h"

#define UE_API COMPOSITE_API

class AActor;
class ACompositeActor;
class UCompositeShadowReflectionCatcherComponent;
class USceneCaptureComponent2D;

/** Automatic primitive configuration mode for registered primitives. */
UENUM(BlueprintType)
enum class ECompositeHiddenInSceneCaptureConfiguration : uint8
{
	None UMETA(ToolTip = "No primitive properties are updated"),
	Visible UMETA(ToolTip = "The following properties are set to false: bHiddenInSceneCapture, bAffectIndirectLightingWhileHidden & bCastHiddenShadow."),
	Hidden UMETA(ToolTip = "The following properties are set to true: bHiddenInSceneCapture, bAffectIndirectLightingWhileHidden & bCastHiddenShadow."),
};

/** Primitive visibility settings cached state. */
USTRUCT()
struct FCompositeShadowReflectionPrimitiveState
{
public:
	GENERATED_USTRUCT_BODY()

	/** Hidden in scene capture. */
	UPROPERTY()
	bool bHiddenInSceneCapture = false;
	
	/** Affect indirect lighting while hidden. */
	UPROPERTY()
	bool bAffectIndirectLightingWhileHidden = false;
	
	/** Cast hidden shadow. */
	UPROPERTY()
	bool bCastHiddenShadow = false;
};

/**
* Layer for shadow & reflection catching with two additional scene capture renders.
* Primitives are rendered in one scene capture but not the other. The division of both creates a multiplicative matte that can be applied onto the plate.
*/
UCLASS(MinimalAPI, BlueprintType, Blueprintable, EditInlineNew, CollapseCategories, meta = (DisplayName = "Shadow Reflection Catcher"))
class UCompositeLayerShadowReflection : public UCompositeLayerBase
{
	GENERATED_BODY()

public:
	/** Constructor. */
	UE_API UCompositeLayerShadowReflection(const FObjectInitializer& ObjectInitializer);
	
	/** Destructor. */
	UE_API virtual ~UCompositeLayerShadowReflection();

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

	/**
	 * Depending on the configuration mode, we automatically enable or disable the following (primitive component) properties:
	 * 
	 * * bHiddenInSceneCapture
	 * * bAffectIndirectLightingWhileHidden
	 * * bCastHiddenShadow
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "", meta = (DisplayAfter = "Actors", DisplayName = "Auto-Configure Actors"))
	ECompositeHiddenInSceneCaptureConfiguration AutoConfigureActors;

private:

	/** Update scene capture visibility on registered mesh actors primitives. */
	void UpdatePrimitiveVisibilityState(bool bInHiddenInSceneCapture, bool bInAffectIndirectLightingWhileHidden, bool bInCastHiddenShadow) const;

	/** Restore scene capture visibility on registered mesh actors primitives. */
	void RestorePrimitiveVisibilityState() const;

	/** Update scene capture hidden components. */
	void UpdateSceneCaptureComponents(USceneCaptureComponent2D& SceneCaptureComponent) const;

	/** 5.7 hotfix: End of frame callback. */
	void OnEndOfFrameUpdate(UWorld* InWorld);

#if WITH_EDITOR
	/** 5.7 hotfix: Replacement for PreEditChange which can't be override in a hotfix. */
	void OnObjectPreEditChange(UObject* Object, const FEditPropertyChain& EditPropertyChain);
#endif

	/** Find or create scene captures with CG and without CG. */
	TStaticArray<TWeakObjectPtr<UCompositeShadowReflectionCatcherComponent>, 2> FindOrCreateSceneCapturePair(ACompositeActor& InOuter) const;

private:
	/** List of reflection & shadow caster actors. */
	UPROPERTY(EditAnywhere, BlueprintGetter = GetActors, BlueprintSetter = SetActors, Category = "")
	TArray<TSoftObjectPtr<AActor>> Actors;

#if WITH_EDITORONLY_DATA
	UE_DEPRECATED(5.7, "CachedVisibilityInSceneCapture has been deprecated in the 5.7.2 hotfix.")
	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "CachedVisibilityInSceneCapture has been deprecated in the 5.7.2 hotfix."))
	TMap<TWeakObjectPtr<UPrimitiveComponent>, FCompositeShadowReflectionPrimitiveState> CachedVisibilityInSceneCapture;
#endif

	/** Cached pointer to the scene captures with CG and without CG. */
	mutable TStaticArray<TWeakObjectPtr<UCompositeShadowReflectionCatcherComponent>, 2> CachedSceneCaptures;

	friend class FCompositeLayerShadowReflectionCustomization;
};

#undef UE_API


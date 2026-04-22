// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Containers/ContainersFwd.h"
#include "UObject/Object.h"

#include "Passes/CompositePassBase.h"
#include "Passes/CompositeCorePassMergeProxy.h"

#include "CompositeLayerBase.generated.h"

#define UE_API COMPOSITE_API

class ACompositeActor;
class UWorld;

/** Base class for defining a pass object, and its associated render proxy. */
UCLASS(MinimalAPI, Abstract, EditInlineNew)
class UCompositeLayerBase : public UObject
{
	GENERATED_BODY()

public:
	/** Context used during layer to proxy assembly. */
	struct FTraversalContext
	{
		/** Flag to indicate solo pass */
		bool bIsSolo = false;
		
		/** Flag to indicate first pass */
		bool bIsFirstPass = true;

		/** Flag to indicate that a layer is independent of scene textures. Work from scene-independent layers can be shared between post-processing locations. */
		bool bNeedsSceneTextures = false;
		
		/** Array of passes applied at the start of rendering. */
		TSortedMap<UE::CompositeCore::ResourceId, TArray<const FCompositeCorePassProxy*>> PreprocessingPasses;

		/**
		* Find or create an external texture resource.
		* 
		* @return ResourceId Texture resource identifier
		*/
		UE::CompositeCore::ResourceId FindOrCreateExternalTexture(TWeakObjectPtr<UTexture> InTexture, UE::CompositeCore::FResourceMetadata InMetadata);

		/** Get external textures. */
		const TArray<UE::CompositeCore::FExternalTexture>& GetExternalTextures() const;

	private:
		/** List of external textures */
		TArray<UE::CompositeCore::FExternalTexture> ExternalTextures;
	};

	/** Constructor. */
	UE_API UCompositeLayerBase(const FObjectInitializer& ObjectInitializer);
	
	/** Destructor. */
	UE_API virtual ~UCompositeLayerBase();

#if WITH_EDITOR
	UE_API virtual bool CanEditChange(const FProperty* InProperty) const;
	UE_API virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	/**
	* Called when composite actor rendering state changes.
	* 
	* @param bApply - Apply or remove external rendering state.
	*/
	UE_API virtual void OnRenderingStateChange(bool bApply) {}

	/**
	* Called when the layer is removed or composite actor is destroyed.
	*
	* @param World - World from which the layer is removed. Ignore if null.
	*/
	UE_API virtual void OnRemoved(const UWorld* World) {}

	/** Getter function to override, returning pass layer proxies to be passed to the render thread. (Proxy objects should be allocated from the provided allocator.) */
	UE_API virtual bool GetProxy(FTraversalContext& InContext, FSceneRenderingBulkObjectAllocator& InFrameAllocator, FCompositeCorePassProxy*& OutProxy) const { return false; };

	/** Get the enabled state. */
	UFUNCTION(BlueprintGetter)
	UE_API virtual bool IsEnabled() const;

	/** Set the enabled state. */
	UFUNCTION(BlueprintSetter)
	UE_API virtual void SetEnabled(bool bInEnabled);

public:
	/** Whether or not the pass is solo. */
	UPROPERTY()
	bool bIsSolo = false;

#if WITH_EDITORONLY_DATA
	/** Pass layer name. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "", meta = (DisplayPriority = "1"))
	FString Name;
#endif

	/** Merge operation applied on Input0 with Input1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "", meta = (DisplayPriority = "3"))
	ECompositeCoreMergeOp Operation = ECompositeCoreMergeOp::Over;

protected:
	/** Whether or not the pass is enabled. */
	UPROPERTY(EditAnywhere, BlueprintGetter = IsEnabled, BlueprintSetter = SetEnabled, Category = "", meta = (DisplayPriority = "2"))
	bool bIsEnabled = true;

	/** Convenience function to register post-processing child passes. */
	void AddChildPasses(UE::CompositeCore::FPassInputDecl& InBasePassInput, FTraversalContext& InContext, FSceneRenderingBulkObjectAllocator& InFrameAllocator, TArrayView<const TObjectPtr<UCompositePassBase>> InPasses) const;

	/** Fixed number of inputs used by layer merge passes. */
	static constexpr int32 FixedNumLayerInputs = 2;

	/** Convenience function that returns the composite actor's render resolution. */
	const FIntPoint GetRenderResolution() const;

	/** Convenience function that returns the merge operation depending on the traversal context. */
	ECompositeCoreMergeOp GetMergeOperation(const FTraversalContext& InContext) const;

	/** Convenience function that returns the default secondary input depending on the traversal context. */
	UE::CompositeCore::FPassInputDecl GetDefaultSecondInput(const FTraversalContext& InContext) const;
};

#undef UE_API


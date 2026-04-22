// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Containers/ContainersFwd.h"
#include "UObject/Object.h"

#include "Passes/CompositeCorePassProxy.h"

#include "CompositePassBase.generated.h"

#define UE_API COMPOSITE_API

/** Base class for defining a pass object, and its associated render proxy. */
UCLASS(MinimalAPI, Abstract, EditInlineNew)
class UCompositePassBase : public UObject
{
	GENERATED_BODY()

public:
	/** Constructor. */
	UE_API UCompositePassBase(const FObjectInitializer& ObjectInitializer);
	
	/** Destructor. */
	UE_API virtual ~UCompositePassBase();

#if WITH_EDITOR
	UE_API virtual bool CanEditChange(const FProperty* InProperty) const;
#endif

	/** Getter function to override, returning pass proxies to be passed to the render thread. (Proxy objects should be allocated from the provided allocator.) */
	UE_API virtual bool GetProxy(const UE::CompositeCore::FPassInputDecl& InputDecl, FSceneRenderingBulkObjectAllocator& InFrameAllocator, FCompositeCorePassProxy*& OutProxy) const { return false; };

	/** Get the enabled state. */
	UFUNCTION(BlueprintGetter)
	UE_API virtual bool IsEnabled() const;

	/** Set the enabled state. */
	UFUNCTION(BlueprintSetter)
	UE_API virtual void SetEnabled(bool bInEnabled);

	/** Returns true if the pass is returns a valid proxy. */
	virtual bool IsActive() const { return IsEnabled(); }

	/** Returns true if the pass (may) need post-processing scene textures. */
	virtual bool NeedsSceneTextures() const { return false; }

#if WITH_EDITOR
	/** Gets the display name of the pass */
	FString GetDisplayName() const { return DisplayName; }

	/** Sets the display name of the pass */
	void SetDisplayName(const FString& InDisplayName) { DisplayName = InDisplayName; }
#endif
	
protected:
	/** Whether or not the pass is active. */
	UPROPERTY(EditAnywhere, BlueprintGetter = IsEnabled, BlueprintSetter = SetEnabled, Category = "", meta = (DisplayPriority = "1"))
	bool bIsEnabled = true;

#if WITH_EDITORONLY_DATA
	/** The display name of the pass */
	UPROPERTY(EditAnywhere, Category = "", meta = (DisplayName="Name", DisplayPriority = "0"))
	FString DisplayName;
#endif
};

#undef UE_API


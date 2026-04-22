// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "CompositePassBase.h"
#include "Passes/CompositeCorePassMergeProxy.h"

#include "CompositePassDistortion.generated.h"

#define UE_API COMPOSITE_API

class FLensDistortionSceneViewExtension;

UENUM(BlueprintType)
enum class ECompositeDistortion : uint8
{
	Undistort,
	Distort,
};

/** Pass to apply an Distortion transform. */
UCLASS(MinimalAPI, BlueprintType, Blueprintable, EditInlineNew, CollapseCategories, meta = (DisplayName = "Distortion Pass"))
class UCompositePassDistortion : public UCompositePassBase
{
	GENERATED_BODY()

public:
	/** Constructor */
	UE_API UCompositePassDistortion(const FObjectInitializer& ObjectInitializer);
	/** Destructor */
	UE_API ~UCompositePassDistortion();

	//~ Begin UObject Interface
	virtual void PostInitProperties() override;
	//~ End UObject Interface

	virtual bool IsActive() const override;

	UE_API virtual bool GetProxy(const UE::CompositeCore::FPassInputDecl& InputDecl, FSceneRenderingBulkObjectAllocator& InFrameAllocator, FCompositeCorePassProxy*& OutProxy) const override;

public:
	/** Distortion to apply on input. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "")
	ECompositeDistortion Distortion;

private:
	/** Cached lens distortion scene view extension. */
	TSharedPtr<FLensDistortionSceneViewExtension, ESPMode::ThreadSafe> LensDistortionSVE;
};

#undef UE_API

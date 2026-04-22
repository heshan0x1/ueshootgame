// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CompositePassBase.h"

#include "CompositePassCenteredScale.generated.h"

#define UE_API COMPOSITE_API

/** Primary centered scale calculation mode.*/
UENUM()
enum class ECompositePassScaleMode : uint8
{
	/** Disabled */
	None,

	/** Automatically derive aspect ratio from the parent layer media texture & composite actor camera. */
	Automatic,

	/** Calculate the scaling factor from source & target aspect ratios.*/
	AspectRatio,

	/** Manually define the scaling factor.*/
	Manual,
};

/** Overscan uncrop derivation mode. */
UENUM()
enum class ECompositePassOverscanUncropMode : uint8
{
	/** Disabled */
	None,

	/** Automatically derive the overscan crop factor from the parent composite actor camera reference. */
	Automatic,

	/** Manually define the overscan crop factor.*/
	Manual,
};

/** Convenience pass to re-scale footage (with black bars) inside its container texture. */
UCLASS(MinimalAPI, BlueprintType, Blueprintable, EditInlineNew, CollapseCategories, meta=(DisplayName="Centered Scale Pass"))
class UCompositePassCenteredScale : public UCompositePassBase
{
	GENERATED_BODY()

public:
	/** Constructor */
	UE_API UCompositePassCenteredScale(const FObjectInitializer& ObjectInitializer);
	
	/** Destructor */
	UE_API ~UCompositePassCenteredScale();

	UE_API virtual bool GetProxy(const UE::CompositeCore::FPassInputDecl& InputDecl, FSceneRenderingBulkObjectAllocator& InFrameAllocator, FCompositeCorePassProxy*& OutProxy) const override;

	UE_API virtual bool IsActive() const override;

	/** Calculate the final texture UV scale. */
	UFUNCTION(BlueprintCallable, Category = "Composite")
	UE_API FVector2f CalculateScale() const;

public:
	/** Centered scale calculation mode used to rescale a media texture with black bars into an already constrained aspect ratio viewport. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "")
	ECompositePassScaleMode ScaleMode;

	/** Source container aspect ratio (or resolution). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "", meta = (EditCondition = "ScaleMode == ECompositePassScaleMode::AspectRatio"))
	FVector2f SourceAspectRatio;

	/** Embedded target aspect ratio (or resolution), without black bars. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "", meta = (EditCondition = "ScaleMode == ECompositePassScaleMode::AspectRatio"))
	FVector2f TargetAspectRatio;

	/** Manual scale factor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "", meta = (EditCondition = "ScaleMode == ECompositePassScaleMode::Manual"))
	FVector2f ScaleFactor;

	/** Uncrop calculation mode used to uncrop a viewport cropped in by overscan. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "")
	ECompositePassOverscanUncropMode OverscanUncropMode;

	/** Manual overscan to uncrop, with values from 0.0 to 1.0 matching the source camera overscan. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "", meta = (EditCondition = "OverscanUncropMode == ECompositePassOverscanUncropMode::Manual"))
	float Overscan;
};

#undef UE_API


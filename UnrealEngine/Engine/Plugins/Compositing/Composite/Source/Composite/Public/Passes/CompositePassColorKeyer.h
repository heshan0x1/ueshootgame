// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CompositePassBase.h"

#include "CompositePassColorKeyer.generated.h"

#define UE_API COMPOSITE_API

/** Visualization mode, for inspecting your key.*/
UENUM(BlueprintType)
enum class ECompositeColorKeyerVisualization : uint8
{
	Key = 0,
	Fill = 1,
	Alpha = 2
};

/** Background screen color type (red, green or blue).*/
UENUM(BlueprintType)
enum class ECompositeColorKeyerScreenType : uint8
{
	Red = 0,
	Green = 1,
	Blue = 2
};

/** Denoising method applied before keying. */
UENUM(BlueprintType)
enum class ECompositeDenoiseMethod : uint8
{
	None,
	Median3x3 UMETA(DisplayName = "Median 3x3"),
};


/**
 * Color keyer with clean plate & spill removal support.
*/
UCLASS(MinimalAPI, BlueprintType, Blueprintable, EditInlineNew, CollapseCategories, meta=(DisplayName="Color Keyer Pass"))
class UCompositePassColorKeyer : public UCompositePassBase
{
	GENERATED_BODY()

public:
	/** Constructor */
	UE_API UCompositePassColorKeyer(const FObjectInitializer& ObjectInitializer);
	
	/** Destructor */
	UE_API ~UCompositePassColorKeyer();

	UE_API virtual bool GetProxy(const UE::CompositeCore::FPassInputDecl& InputDecl, FSceneRenderingBulkObjectAllocator& InFrameAllocator, FCompositeCorePassProxy*& OutProxy) const override;

#if WITH_EDITOR
	UE_API virtual void PreEditChange(FProperty* PropertyThatWillChange) override;
	UE_API virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

public:
	/** Type of screen color (required). The keyer works best against red, green or blue backgrounds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "")
	ECompositeColorKeyerScreenType ScreenType;

	/** Static background key color.*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "", meta = (OnlyUpdateOnInteractionEnd, EditCondition = "CleanPlate == nullptr"))
	FLinearColor KeyColor;

	/**
	 * Clean plate background for calculating color differences per pixel, instead of the static key color.
	 * Resolution must match the composite actor render resolution.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "")
	TObjectPtr<UTexture> CleanPlate;

	/** Weight of the foreground red channel contributing to the key matte hardness. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "", meta = (UIMin = 0.0f, UIMax = 1.0f, EditCondition = "ScreenType != ECompositeColorKeyerScreenType::Red"))
	float RedWeight;

	/** Weight of the foreground green channel contributing to the key matte hardness. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "", meta = (UIMin = 0.0f, UIMax = 1.0f, EditCondition = "ScreenType != ECompositeColorKeyerScreenType::Green"))
	float GreenWeight;

	/** Weight of the foreground blue channel contributing to the key matte hardness. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "", meta = (UIMin = 0.0f, UIMax = 1.0f, EditCondition = "ScreenType != ECompositeColorKeyerScreenType::Blue"))
	float BlueWeight;

	/** Thresholds any alpha value outside the specified range to zero or one respectively, with linear interpolation in-between. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "", meta = (UIMin = 0.0f, UIMax = 1.0f))
	FVector2f AlphaThreshold;

	/** Strength of the spill reduction, 0.0: none, 1.0: full. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "", meta = (UIMin = 0.0f, UIMax = 1.0f))
	float DespillStrength;

	/** Strength of the vignette removal. Used to improve plate uniformity & remove darker corners. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "", meta = (UIMin = 0.0f, UIMax = 1.0f))
	float DevignetteStrength;

	/** When enabled, we undo devignetting before outputting the keyed plate. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "")
	bool bPreserveVignetteAfterKey;

	/** Denoising method applied before the keyer. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "")
	ECompositeDenoiseMethod DenoiseMethod;

	/** Vizualize the alpha key or fill. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "")
	ECompositeColorKeyerVisualization Visualization;

	/** Invert the alpha key. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "")
	bool bInvertAlpha;

#if WITH_EDITOR
	/** Flag tracking whether we disabled throttling to later restore it. */
	bool bEditorThrottleDisabled = false;
#endif
};

#undef UE_API


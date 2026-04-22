// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "CompositePassBase.h"
#include "Passes/CompositeCorePassMergeProxy.h"

#include "CompositePassColorGrade.generated.h"

#define UE_API COMPOSITE_API

/** Color grading pass temperature settings. */
USTRUCT(BlueprintType)
struct FCompositeTemperatureSettings
{
	GENERATED_USTRUCT_BODY()

	/**
	* Selects the type of temperature calculation.
	* White Balance uses the Temperature value to control the virtual camera's White Balance. This is the default selection.
	* Color Temperature uses the Temperature value to adjust the color temperature of the scene, which is the inverse of the White Balance operation.
	*/
	UPROPERTY(interp, BlueprintReadWrite, Category = "Color Grading|Temperature", meta = (DisplayName = "Temperature Type"))
	TEnumAsByte<enum ETemperatureMethod> TemperatureType = ETemperatureMethod::TEMP_WhiteBalance;

	/** Controls the color temperature or white balance in degrees Kelvin which the scene considers as white light. */
	UPROPERTY(interp, BlueprintReadWrite, Category = "Color Grading|Temperature", meta = (UIMin = "1500.0", UIMax = "15000.0", DisplayName = "Temp"))
	float WhiteTemp = 6500.0f;
	
	/** Controls the color of the scene along the magenta - green axis (orthogonal to the color temperature).  This feature is equivalent to color tint in digital cameras. */
	UPROPERTY(interp, BlueprintReadWrite, Category = "Color Grading|Temperature", meta = (UIMin = "-1.0", UIMax = "1.0", DisplayName = "Tint"))
	float WhiteTint = 0.0f;

};

/**
 * Color grade pass.
 * Assumes input is in linear working color space.
*/
UCLASS(MinimalAPI, BlueprintType, Blueprintable, EditInlineNew, CollapseCategories, meta = (DisplayName = "Color Grade Pass"))
class UCompositePassColorGrade : public UCompositePassBase
{
	GENERATED_BODY()

public:
	/** Constructor */
	UE_API UCompositePassColorGrade(const FObjectInitializer& ObjectInitializer);
	/** Destructor */
	UE_API ~UCompositePassColorGrade();

	UE_API virtual bool GetProxy(const UE::CompositeCore::FPassInputDecl& InputDecl, FSceneRenderingBulkObjectAllocator& InFrameAllocator, FCompositeCorePassProxy*& OutProxy) const override;

public:
	/** Color temperature settings. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "")
	FCompositeTemperatureSettings TemperatureSettings;

	/** Color grade settings. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "", meta = (ShowPostProcessCategories))
	FColorGradingSettings ColorGradingSettings;
};

#undef UE_API

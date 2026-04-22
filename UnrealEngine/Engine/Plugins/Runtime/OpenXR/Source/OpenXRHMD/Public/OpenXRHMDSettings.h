// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettingsBackedByCVars.h"
#include "OpenXRHMDSettings.generated.h"

/**
* Implements the settings for the OpenXR plugin.
*/
UCLASS(config=Engine, defaultconfig, meta = (DisplayName = "OpenXR Settings"))
class OPENXRHMD_API UOpenXRHMDSettings : public UDeveloperSettingsBackedByCVars
{
	GENERATED_BODY()

public:
	/** Enables foveation provided by the XR_FB_foveation OpenXR extension. */
	UPROPERTY(config, EditAnywhere, Category = "Foveation", meta = (
		ToolTip = "Enables foveation provided by the XR_FB_foveation OpenXR extension. Requires support for hardware variable rate shading.", 
		DisplayName = "Enable XR_FB_foveation extension"))
	bool bIsFBFoveationEnabled = false;

	/** Enables alpha inversion of the background layer. */
	UPROPERTY(config, EditAnywhere, Category = "Passthrough", meta = (
		ConsoleVariable = "xr.OpenXRInvertAlpha",
		ToolTip = "Enables alpha inversion of the background layer if the XR_EXT_composition_layer_inverted_alpha extension or XR_FB_composition_layer_alpha_blend is supported.", 
		DisplayName = "Invert scene alpha for passthrough"))
	bool bOpenXRInvertAlpha = false;

	/** Enable support for OpenXR 1.0. */
	UPROPERTY(config, EditAnywhere, Category = "OpenXR Versions", meta = (
		ToolTip = "Enable support for OpenXR 1.0. If multiple versions are supported by the current OpenXR Runtime the latest version will be used.",
		DisplayName = "Enable OpenXR 1.0"))
	bool bIsOpenXR1_0Enabled = true;

	/** Enable support for OpenXR 1.1. */
	UPROPERTY(config, EditAnywhere, Category = "OpenXR Versions", meta = (
		ToolTip = "Enable support for OpenXR 1.1. If multiple versions are supported by the current OpenXR Runtime the latest version will be used.",
		DisplayName = "Enable OpenXR 1.1"))
	bool bIsOpenXR1_1Enabled = true;

public:
	virtual FName GetCategoryName() const override { return FName(TEXT("Plugins")); }
};

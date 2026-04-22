// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Object.h"
#include "MediaFrameworkWorldSettingsAssetUserData.h"

#include "MediaProfileEditorUserSettings.generated.h"

/** Config class to store editor user settings for the media profile editor */
UCLASS(MinimalAPI, Config=EditorPerProjectUserSettings)
class UMediaProfileEditorUserSettings : public UObject
{
	GENERATED_BODY()
	
public:
	/** Whether to show the timecode entry in the viewport toolbar */
	UPROPERTY(Config)
	bool bShowTimecodeInViewportToolbar = true;

	/** Whether to show the genlock entry in the viewport toolbar */
	UPROPERTY(Config)
	bool bShowGenlockInViewportToolbar = true;
};

/**
 * Variant of the media capture settings config for media outputs in the media profile
 */
UCLASS(MinimalAPI, config = EditorSettings)
class UMediaProfileEditorCaptureSettings : public UMediaFrameworkWorldSettingsAssetUserData
{
	GENERATED_BODY()
	
public:
	/** Should the capture be restarted if the media output is modified. */
	UPROPERTY(config)
	bool bAutoRestartCaptureOnChange = true;
};
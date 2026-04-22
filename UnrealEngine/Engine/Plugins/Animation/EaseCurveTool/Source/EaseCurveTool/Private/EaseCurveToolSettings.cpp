// Copyright Epic Games, Inc. All Rights Reserved.

#include "EaseCurveToolSettings.h"

#define LOCTEXT_NAMESPACE "EaseCurveToolSettings"

UEaseCurveToolSettings::UEaseCurveToolSettings()
{
	CategoryName = TEXT("Sequencer");
	SectionName = TEXT("Ease Curve Tool");

	DefaultPresetLibrary = FSoftObjectPath(TEXT("/EaseCurveTool/DefaultPresetLibrary.DefaultPresetLibrary"));

	NewPresetCategory = LOCTEXT("NewPresetCategory", "Custom");
}

#undef LOCTEXT_NAMESPACE

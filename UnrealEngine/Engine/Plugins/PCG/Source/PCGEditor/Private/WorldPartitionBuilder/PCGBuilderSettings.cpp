// Copyright Epic Games, Inc. All Rights Reserved.

#include "PCGBuilderSettings.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(PCGBuilderSettings)

UPCGBuilderSettings::UPCGBuilderSettings()
{
	// By default include this mode
	EditingModes.Add(EPCGEditorDirtyMode::LoadAsPreview);
}

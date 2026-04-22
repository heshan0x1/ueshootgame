// Copyright Epic Games, Inc. All Rights Reserved.

#include "FacialAnimationBulkImporterSettings.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(FacialAnimationBulkImporterSettings)

void UFacialAnimationBulkImporterSettings::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	SaveConfig();
}

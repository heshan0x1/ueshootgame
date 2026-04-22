// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanGenerateDepthWindowOptions.h"

#include "MetaHumanDepthGeneratorModule.h"

#include "Modules/ModuleManager.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MetaHumanGenerateDepthWindowOptions)

const FString UMetaHumanGenerateDepthWindowOptions::ImageSequenceDirectoryName = TEXT("GeneratedDepthData");

#if WITH_EDITOR
void UMetaHumanGenerateDepthWindowOptions::PostEditChangeProperty(FPropertyChangedEvent& InPropertyChangedEvent)
{
	Super::PostEditChangeProperty(InPropertyChangedEvent);

	MinDistance = FMath::Clamp(MinDistance, 0.0, MaxDistance);
}
#endif

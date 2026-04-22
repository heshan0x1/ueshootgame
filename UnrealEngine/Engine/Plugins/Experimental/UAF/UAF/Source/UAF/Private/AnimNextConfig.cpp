// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimNextConfig.h"

#include "Module/RigUnit_AnimNextModuleEvents.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AnimNextConfig)

#if WITH_EDITOR

void UAnimNextConfig::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	SaveConfig();
}

#endif

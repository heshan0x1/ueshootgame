// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PCGGenerateLandscapeTextures.h"

#include "PCGGenerateGrassMaps.generated.h"

UCLASS(MinimalAPI, BlueprintType, Deprecated, meta = (DeprecationMessage = "Deprecated 5.7. Use UPCGGenerateLandscapeTexturesSettings instead."))
class UDEPRECATED_PCGGenerateGrassMapsSettings : public UPCGGenerateLandscapeTexturesSettings
{
	GENERATED_BODY()
};

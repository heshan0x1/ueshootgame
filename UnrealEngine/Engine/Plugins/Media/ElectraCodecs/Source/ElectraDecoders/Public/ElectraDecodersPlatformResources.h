// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#if ELECTRA_DECODERS_HAVE_PLATFORM_DEFAULTS
#include COMPILED_PLATFORM_HEADER(ElectraDecodersPlatformResources.h)
#else

class FElectraDecodersPlatformResourcesNull
{
public:
};

using FElectraDecodersPlatformResources = FElectraDecodersPlatformResourcesNull;

#endif

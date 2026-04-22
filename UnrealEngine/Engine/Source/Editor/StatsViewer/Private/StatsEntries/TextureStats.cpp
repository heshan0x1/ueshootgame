// Copyright Epic Games, Inc. All Rights Reserved.

#include "TextureStats.h"

#include "UObject/WeakObjectPtr.h"

#include <cfloat>

#include UE_INLINE_GENERATED_CPP_BY_NAME(TextureStats)

UTextureStats::UTextureStats(const FObjectInitializer& ObjectInitializer) :
	Super(ObjectInitializer),
	LastTimeRendered( FLT_MAX )
{
}

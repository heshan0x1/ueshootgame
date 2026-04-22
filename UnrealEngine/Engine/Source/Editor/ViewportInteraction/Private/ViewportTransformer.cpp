// Copyright Epic Games, Inc. All Rights Reserved.

#include "ViewportTransformer.h"
#include "ViewportWorldInteraction.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(ViewportTransformer)

PRAGMA_DISABLE_DEPRECATION_WARNINGS


void UViewportTransformer::Init( UViewportWorldInteraction* InitViewportWorldInteraction )
{
	this->ViewportWorldInteraction = InitViewportWorldInteraction;
}


void UViewportTransformer::Shutdown()
{
	this->ViewportWorldInteraction = nullptr;
}


PRAGMA_ENABLE_DEPRECATION_WARNINGS

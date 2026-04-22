// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class FRDGTexture;


/*
 * Holds overlay drawing parameters
 */
struct FDisplayClusterShaderParameters_Overlay
{
	/** Base texture (bottom layer) */
	FRDGTexture* BaseTexture = nullptr;

	/** Overlay texture (top layer) */
	FRDGTexture* OverlayTexture = nullptr;

	/** Output texture (the result of source-over blending) */
	FRDGTexture* OutputTexture = nullptr;

public:

	/** Parameters validation */
	bool IsValidData() const
	{
		return BaseTexture && OverlayTexture && OutputTexture;
	}
};

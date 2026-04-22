// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DisplayClusterViewport_Enums.h"

#include "Math/MarginSet.h"

/**
* Overscan settings of viewport
*/
struct FDisplayClusterViewport_OverscanSettings
	: public FMarginSet
{
	// Enable overscan
	uint8 bEnabled : 1 = 0;

	// Set to True to render at the overscan resolution, set to false to render at the resolution in the configuration and scale for overscan
	uint8 bOversize : 1 = 0;

	// Blends the overlapping (overscan) regions between adjacent tiles.
	// The size (in pixels) must be less than or equal to the overscan size,
	// ensuring a smooth transition with no visible seams.
	uint8 bApplyTileEdgeBlend : 1 = 0;

	// Percentage of the overscan (0.0–1.0) to use for edge blending.
	float TileEdgeBlendPercentage = 0.0f;

	// Units type of overscan values
	EDisplayClusterViewport_FrustumUnit Unit = EDisplayClusterViewport_FrustumUnit::Pixels;
};

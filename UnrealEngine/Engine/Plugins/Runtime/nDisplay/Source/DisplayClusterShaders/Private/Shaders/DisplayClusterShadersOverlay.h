// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class FRDGBuilder;
struct FDisplayClusterShaderParameters_Overlay;


/**
 * API for overlay drawing
 */
class FDisplayClusterShadersOverlay
{
public:

	/** Draws overlay on top of the base texture (refer function arguments) */
	static void AddOverlayBlendingPass(FRDGBuilder& GraphBuilder, const FDisplayClusterShaderParameters_Overlay& Parameters);
};

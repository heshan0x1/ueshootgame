// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"

enum EPixelFormat : uint8;

namespace LumenShortRangeAO
{
	bool ShouldApplyDuringIntegration();
	bool UseBentNormal();
	EPixelFormat GetTextureFormat();
	uint32 GetDownsampleFactor();
	uint32 GetRequestedDownsampleFactor();
	bool UseTemporal();
	float GetTemporalNeighborhoodClampScale();
	float GetFoliageOcclusionStrength();
};
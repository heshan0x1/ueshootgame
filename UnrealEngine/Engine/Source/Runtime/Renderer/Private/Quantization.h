// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	Quantization.h: common quantization functions used by renderer.
=============================================================================*/

#pragma once

#include "Math/Vector.h"
#include "PixelFormat.h"

FVector3f ComputePixelFormatQuantizationError(EPixelFormat PixelFormat);

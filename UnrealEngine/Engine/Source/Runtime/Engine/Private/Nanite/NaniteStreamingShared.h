// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Rendering/NaniteResources.h"

namespace Nanite
{

// Round up to smallest value greater than or equal to x of the form k*2^s where k < 2^NumSignificantBits.
// This is the same as RoundUpToPowerOfTwo when NumSignificantBits=1.
// For larger values of NumSignificantBits each po2 bucket is subdivided into 2^(NumSignificantBits-1) linear steps.
// This gives more steps while still maintaining an overall exponential structure and keeps numbers nice and round (in the po2 sense).

// Example:
// Representable values for different values of NumSignificantBits.
// 1: ..., 16, 32, 64, 128, 256, 512, ...
// 2: ..., 16, 24, 32,  48,  64,  96, ...
// 3: ..., 16, 20, 24,  28,  32,  40, ...
FORCEINLINE uint32 RoundUpToSignificantBits(uint32 x, uint32 NumSignificantBits)
{
	check(NumSignificantBits <= 32);

	const int32_t Shift = FMath::Max((int32)FMath::CeilLogTwo(x) - (int32)NumSignificantBits, 0);
	const uint32 Mask = (1u << Shift) - 1u;
	return (x + Mask) & ~Mask;
}

struct FGPUStreamingRequest
{
	uint32			RuntimeResourceID_Magic;
	FPageRangeKey	ResourcePageRangeKey;
	uint32			Priority_Magic;
};

} // namespace Nanite
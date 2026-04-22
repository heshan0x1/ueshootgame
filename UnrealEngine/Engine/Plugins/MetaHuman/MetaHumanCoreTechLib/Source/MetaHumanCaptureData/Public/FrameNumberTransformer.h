// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Misc/FrameRate.h"

#define UE_API METAHUMANCAPTUREDATA_API

namespace UE::MetaHuman
{

class FFrameNumberTransformer
{
public:
	/** Pass-through transform (no change) */
	UE_API FFrameNumberTransformer();

	/** Apply a simple offset */
	UE_API explicit FFrameNumberTransformer(int32 InFrameNumberOffset);

	/** Adjust for differences in frame rate */
	UE_API FFrameNumberTransformer(FFrameRate InSourceFrameRate, FFrameRate InTargetFrameRate);

	/** Adjust for differences in frame rate and apply an offset */
	UE_API FFrameNumberTransformer(FFrameRate InSourceFrameRate, FFrameRate InTargetFrameRate, int32 InFrameNumberOffset);

	/* Transforms the input frame number */
	UE_API int32 Transform(int32 InFrameNumber) const;

private:
	FFrameRate SourceFrameRate;
	FFrameRate TargetFrameRate;
	int32 SkipFactor = 0;
	int32 DuplicationFactor = 0;
	int32 FrameNumberOffset = 0;
};

}

#undef UE_API

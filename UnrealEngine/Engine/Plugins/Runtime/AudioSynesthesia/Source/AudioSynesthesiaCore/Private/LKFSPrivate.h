// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "HAL/Platform.h"

namespace Audio::LKFSPrivate
{
	// There may be multiple calls to the LoudnessAnalyzer to produce a single 
	// FLoundessDatum. The sliding window hop is tuned here so that it best 
	// matches the desired AnalysisPeriod while maintaining between a 25% to 
	// 75% window overlap.
	void TuneSlidingWindwoHopSize(int32 InNumAnalysisHopFrames, int32 InNumSlidingWindowFrames, int32& InNumSlidingWindowHopFrames, int32& InNumAnalysisHopWindows);
}

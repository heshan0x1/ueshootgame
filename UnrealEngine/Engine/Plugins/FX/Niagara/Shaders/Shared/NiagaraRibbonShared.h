// Copyright Epic Games, Inc. All Rights Reserved.
// This file is shared between C++ & HLSL

#pragma once

#ifndef GPU_SIMULATION
	#define uint	uint32
#endif

struct FRibbonAccumulationValues
{
	float RibbonDistance;
	uint SegmentCount;
	uint MultiRibbonCount;			// Only valid when HAS_RIBBON_ID
	float TessTotalLength;			// Only valid when RIBBONS_WANTS_AUTOMATIC_TESSELLATION
	float TessAvgSegmentLength;		// Only valid when RIBBONS_WANTS_AUTOMATIC_TESSELLATION
	float TessAvgSegmentAngle;		// Only valid when RIBBONS_WANTS_AUTOMATIC_TESSELLATION
	float TessTwistAvgAngle;		// Only valid when RIBBONS_WANTS_AUTOMATIC_TESSELLATION && RIBBON_HAS_TWIST
	float TessTwistAvgWidth;		// Only valid when RIBBONS_WANTS_AUTOMATIC_TESSELLATION && RIBBON_HAS_TWIST
};

#ifndef GPU_SIMULATION
	#undef uint
#endif

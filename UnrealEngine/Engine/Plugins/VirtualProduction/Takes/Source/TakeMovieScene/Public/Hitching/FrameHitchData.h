// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Misc/Timecode.h"

namespace UE::TakeMovieScene
{
struct FFrameHitchData
{
	/** The timecode frame engine was supposed to have. */
	FTimecode TargetTimecode;
	/** The timecode the frame actually had. */
	FTimecode ActualTimecode;
};
}

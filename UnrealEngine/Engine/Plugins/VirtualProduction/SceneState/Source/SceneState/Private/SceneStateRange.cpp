// Copyright Epic Games, Inc. All Rights Reserved.

#include "SceneStateRange.h"

FSceneStateRange FSceneStateRange::MakeBeginEndRange(uint16 InBegin, uint16 InEnd)
{
	if (InBegin == InvalidIndex || InEnd == InvalidIndex || InBegin >= InEnd)
	{
		return FSceneStateRange();
	}

	FSceneStateRange Range;
	Range.Index = InBegin;
	Range.Count = InEnd - InBegin;
	return Range;
}

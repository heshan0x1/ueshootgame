// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PCGSettings.h"

namespace PCGPolygon2DUtils
{
	namespace Constants
	{
		const FLazyName ClipPolysLabel = TEXT("ClipPolygons");
		const FLazyName ClipPathsLabel = TEXT("ClipPaths");

		// Since these were introduced in 5.7, they will be removed as of 5.8.
		namespace Deprecated
		{
			const FLazyName OldClipPolysLabel = TEXT("Clip Polygons");
			const FLazyName OldClipPathsLabel = TEXT("Clip Paths");
		}
	}

	TArray<FPCGPinProperties> DefaultPolygonInputPinProperties();
	TArray<FPCGPinProperties> DefaultPolygonOutputPinProperties();
}
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

namespace UE::ChaosMover
{
	namespace CVars
	{
		CHAOSMOVER_API extern bool bForceSingleThreadedGT;
		CHAOSMOVER_API extern bool bForceSingleThreadedPT;
		CHAOSMOVER_API extern bool bDrawGroundQueries;
		CHAOSMOVER_API extern bool bDrawOverlapQueries;
		CHAOSMOVER_API extern bool bSkipGenerateMoveIfOverridden;
		CHAOSMOVER_API extern int32 InstantMovementEffectIDHistorySize;
	}
};

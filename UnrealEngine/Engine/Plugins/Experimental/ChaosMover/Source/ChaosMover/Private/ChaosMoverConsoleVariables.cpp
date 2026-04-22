// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosMover/ChaosMoverConsoleVariables.h"

#include "HAL/IConsoleManager.h"

namespace UE::ChaosMover
{
	namespace CVars
	{
		bool bForceSingleThreadedGT = true;
		FAutoConsoleVariableRef CVarChaosMoverForceSingleThreadedGT(TEXT("ChaosMover.ForceSingleThreadedGT"),
			bForceSingleThreadedGT, TEXT("Force updates on the game thread to be single threaded."));

		bool bForceSingleThreadedPT = true;
		FAutoConsoleVariableRef CVarChaosMoverForceSingleThreadedPT(TEXT("ChaosMover.ForceSingleThreadedPT"),
			bForceSingleThreadedPT, TEXT("Force updates on the physics thread to be single threaded."));

		bool bDrawGroundQueries = false;
		FAutoConsoleVariableRef CVarChaosMoverDrawGroundQueries(TEXT("ChaosMover.DebugDraw.GroundQueries"),
			bDrawGroundQueries, TEXT("Draw ground queries."));

		bool bDrawOverlapQueries = false;
		FAutoConsoleVariableRef CVarChaosMoverDrawOverlapQueries(TEXT("ChaosMover.DebugDraw.OverlapQueries"),
			bDrawOverlapQueries, TEXT("Draw overlap queries."));

		bool bSkipGenerateMoveIfOverridden = true;
		FAutoConsoleVariableRef CVarChaosMoverSkipGenerateMoveIfOverridden(TEXT("ChaosMover.Perf.SkipGenerateMoveIfOverridden"),
			bSkipGenerateMoveIfOverridden, TEXT("If true and we have a layered move fully overriding movement, then we will skip calling OnGenerateMove on the active movement mode for better performance\n"));

		int32 InstantMovementEffectIDHistorySize = 30;
		FAutoConsoleVariableRef CVarChaosMoverInstantMovementEffectIDHistorySize(TEXT("ChaosMover.Networking.InstantMovementEffectIDHistorySize"),
			bSkipGenerateMoveIfOverridden, TEXT("Number of past frames for which we keep the last seen instant movement effect IDs, to avoid duplicate processing of events when input is repeated\n"));
	}
}
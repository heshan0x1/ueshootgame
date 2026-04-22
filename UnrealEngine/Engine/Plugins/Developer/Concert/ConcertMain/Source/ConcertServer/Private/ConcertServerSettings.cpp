// Copyright Epic Games, Inc. All Rights Reserved.

#include "ConcertServerSettings.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(ConcertServerSettings)

UConcertServerConfig::UConcertServerConfig()
	: bCleanWorkingDir(false)
	, NumSessionsToKeep(-1)
{
	DefaultVersionInfo.Initialize(false /* bSupportMixedBuildTypes */);
}

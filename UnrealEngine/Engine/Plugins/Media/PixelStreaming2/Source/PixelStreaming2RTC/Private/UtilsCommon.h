// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Misc/CommandLine.h"
namespace UE::PixelStreaming2
{
	static const FString SFU_PLAYER_ID_PREFIX = FString(TEXT("SFU_"));

	inline bool IsSFU(const FString& InPlayerId)
	{
		return InPlayerId.StartsWith(SFU_PLAYER_ID_PREFIX);
	}
} // namespace UE::PixelStreaming2

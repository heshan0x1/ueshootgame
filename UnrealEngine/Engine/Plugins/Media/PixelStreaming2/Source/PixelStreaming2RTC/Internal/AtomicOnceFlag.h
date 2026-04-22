// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include <atomic>

namespace UE::PixelStreaming2
{
	class FAtomicOnceFlag
	{
	public:
		// Sets flag to true and returns if the flag had fired before
		inline bool Trigger() { return bFlag.exchange(true); };
	private:
		std::atomic<bool> bFlag = false;
	};
} // namespace UE::PixelStreaming2

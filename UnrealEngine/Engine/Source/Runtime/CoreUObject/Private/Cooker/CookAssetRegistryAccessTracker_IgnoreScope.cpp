// Copyright Epic Games, Inc. All Rights Reserved.

#include "Cooker/CookAssetRegistryAccessTracker_IgnoreScope.h"

namespace UE::CookAssetRegistryAccessTracker
{
	static thread_local bool bIgnoreAccessTracker = false;

	FIgnoreScope::FIgnoreScope()
		: bPreviousValue(bIgnoreAccessTracker)
	{
		bIgnoreAccessTracker = true;
	}

	FIgnoreScope::~FIgnoreScope()
	{
		bIgnoreAccessTracker = bPreviousValue;
	}

	bool FIgnoreScope::ShouldIgnoreAccessTracker()
	{
		return bIgnoreAccessTracker;
	}
}

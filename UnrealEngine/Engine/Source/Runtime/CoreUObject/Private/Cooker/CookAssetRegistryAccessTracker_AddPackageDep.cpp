// Copyright Epic Games, Inc. All Rights Reserved.

#include "Cooker/CookAssetRegistryAccessTracker_AddPackageDep.h"

namespace UE::CookAssetRegistryAccessTracker
{
	static thread_local bool bAddPackageDependenciesEnabled = false;

	FAddPackageDependenciesScope::FAddPackageDependenciesScope()
		: bPreviousValue(bAddPackageDependenciesEnabled)
	{
		bAddPackageDependenciesEnabled = true;
	}

	FAddPackageDependenciesScope::~FAddPackageDependenciesScope()
	{
		bAddPackageDependenciesEnabled = bPreviousValue;
	}

	bool FAddPackageDependenciesScope::GetAddPackageDependenciesEnabled()
	{
		return bAddPackageDependenciesEnabled;
	}
}

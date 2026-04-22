// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Containers/UnrealString.h"

#define UE_API METAHUMANCORE_API

/** Version for the contour data internal functionality (not necessarily matching UE version) */
struct FMetaHumanContourDataVersion
{
	enum class ECompatibilityResult : uint8
	{
		NoUpgrade = 0,		// Nothing to change
		NeedsUpgrade,		// Not expected to be compatible
		RecommendUpgrade,	// Compatible but upgrade is recommended
		AutoUpgrade,		// Minor change that could be automatically resolved
	};

	/** Returns the Mesh Tracker module version on an FEngineVersion struct */
	static UE_API FString GetContourDataVersionString();

	/** Check for compatibility for a list of versions, returning the most severe upgrade requirement as OutResult */
	static UE_API bool CheckVersionCompatibility(const TArray<FString>& InVersionStringList, ECompatibilityResult& OutResult);

	/** Config file name for contour data */
	UE_API const static FString ConfigFileName;
};

#undef UE_API

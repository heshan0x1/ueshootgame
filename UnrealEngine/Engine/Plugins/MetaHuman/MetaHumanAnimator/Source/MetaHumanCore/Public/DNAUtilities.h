// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#define UE_API METAHUMANCORE_API

enum class EDNARigCompatiblityFlags
{
	None	= 0,

	Joint	= 1 << 0,
	Mesh	= 1 << 1,
	LOD		= 1 << 2,

	All = Joint | Mesh | LOD
};

ENUM_CLASS_FLAGS(EDNARigCompatiblityFlags);

class IDNAReader;

class FDNAUtilities
{
public:
	// Checks if provided DNA readers share the same rig definition
	static UE_API bool CheckCompatibility(IDNAReader* InDnaReaderA, IDNAReader* InDnaReaderB, EDNARigCompatiblityFlags InCompareFlags = EDNARigCompatiblityFlags::All);

	// Checks if provided DNA readers share the same rig definition. Also outputs compatibility message
	static UE_API bool CheckCompatibility(IDNAReader* InDnaReaderA, IDNAReader* InDnaReaderB, EDNARigCompatiblityFlags InCompareFlags, FString& OutCompatibilityMsg);
};

#undef UE_API

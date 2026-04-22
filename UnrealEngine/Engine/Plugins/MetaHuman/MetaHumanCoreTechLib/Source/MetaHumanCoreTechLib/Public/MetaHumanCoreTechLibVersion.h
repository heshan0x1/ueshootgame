// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#define UE_API METAHUMANCORETECHLIB_API


/** Version for TitanLib  */
struct FMetaHumanCoreTechLibVersion
{
	/** Returns the Titan Lib version as a string */
	static UE_API FString GetMetaHumanCoreTechLibVersionString();
};

#undef UE_API

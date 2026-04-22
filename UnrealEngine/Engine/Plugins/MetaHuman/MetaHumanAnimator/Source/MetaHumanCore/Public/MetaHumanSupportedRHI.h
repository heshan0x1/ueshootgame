// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#define UE_API METAHUMANCORE_API



class FMetaHumanSupportedRHI
{
public:

	static UE_API bool IsSupported();
	static UE_API FText GetSupportedRHINames();

private:

	static UE_API bool bIsInitialized;
	static UE_API bool bIsSupported;
};

#undef UE_API

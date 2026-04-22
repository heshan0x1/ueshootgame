// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#define UE_API METAHUMANPLATFORM_API



class FMetaHumanMinSpec
{
public:

	static UE_API bool IsSupported();
	static UE_API FText GetMinSpec();

private:

	static UE_API bool bIsInitialized;
	static UE_API bool bIsSupported;
};

#undef UE_API

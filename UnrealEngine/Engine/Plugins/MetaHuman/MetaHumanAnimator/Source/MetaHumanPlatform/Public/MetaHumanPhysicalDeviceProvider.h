// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#define UE_API METAHUMANPLATFORM_API

class FMetaHumanPhysicalDeviceProvider
{
public:
	static UE_API bool GetLUIDs(FString& OutUEPhysicalDeviceLUID, TArray<FString>& OutAllPhysicalDeviceLUIDs);
	static UE_API int32 GetVRAMInMB();
};

#undef UE_API

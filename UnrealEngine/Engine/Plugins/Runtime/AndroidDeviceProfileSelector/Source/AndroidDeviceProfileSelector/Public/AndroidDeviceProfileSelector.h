// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Map.h"
#include "UObject/NameTypes.h"

#define UE_API ANDROIDDEVICEPROFILESELECTOR_API

namespace FAndroidProfileSelectorSourceProperties
{
	static FName SRC_GPUFamily(TEXT("SRC_GPUFamily"));
	static FName SRC_GLVersion(TEXT("SRC_GLVersion"));
	static FName SRC_VulkanAvailable(TEXT("SRC_VulkanAvailable"));
	static FName SRC_VulkanVersion(TEXT("SRC_VulkanVersion"));
	static FName SRC_AndroidVersion(TEXT("SRC_AndroidVersion"));
	static FName SRC_DeviceMake(TEXT("SRC_DeviceMake"));
	static FName SRC_DeviceModel(TEXT("SRC_DeviceModel"));
	static FName SRC_DeviceBuildNumber(TEXT("SRC_DeviceBuildNumber"));
	static FName SRC_UsingHoudini(TEXT("SRC_UsingHoudini"));
	static FName SRC_Hardware(TEXT("SRC_Hardware"));
	static FName SRC_Chipset(TEXT("SRC_Chipset"));
	static FName SRC_HMDSystemName(TEXT("SRC_HMDSystemName"));
	static FName SRC_TotalPhysicalGB(TEXT("SRC_TotalPhysicalGB"));
	static FName SRC_SM5Available(TEXT("SRC_SM5Available"));
	static FName SRC_ResolutionX(TEXT("SRC_ResolutionX"));
	static FName SRC_ResolutionY(TEXT("SRC_ResolutionY"));
	static FName SRC_InsetsLeft(TEXT("SRC_InsetsLeft"));
	static FName SRC_InsetsTop(TEXT("SRC_InsetsTop"));
	static FName SRC_InsetsRight(TEXT("SRC_InsetsRight"));
	static FName SRC_InsetsBottom(TEXT("SRC_InsetsBottom"));
	static FName SRC_VKQuality(TEXT("SRC_VKQuality"));
};

class FAndroidDeviceProfileSelector
{
	// Container of various device properties used for device profile matching.
	static UE_API TMap<FName, FString> SelectorProperties;
	static UE_API void VerifySelectorParams();
public:

	static UE_API FString FindMatchingProfile(const FString& FallbackProfileName);
	static UE_API int32 GetNumProfiles();
	static void SetSelectorProperties(const TMap<FName, FString>& Params) { SelectorProperties = Params; VerifySelectorParams(); }
	static const TMap<FName, FString>& GetSelectorProperties() { return SelectorProperties; }
};

#undef UE_API

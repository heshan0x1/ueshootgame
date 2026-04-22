// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "CoreTypes.h"

#if USE_ANDROID_JNI

namespace AndroidVKQuality
{
	bool LoadVkQuality(const char* GLESVersionString, void* VulkanPhysicalDeviceProperties);
	void UnloadVkQuality();
	FString GetVKQualityRecommendation();
} // namespace

#endif
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#define UE_API METAHUMANCORETECH_API

class FMetaHumanRealtimeCalibration
{
public:
	
	UE_API FMetaHumanRealtimeCalibration(const TArray<FName>& InProperties, const TArray<float>& InNeutralFrame, const float InAlpha);

	static UE_API TArray<FName> GetDefaultProperties();

	UE_API void SetProperties(const TArray<FName>& InProperties);
	UE_API void SetAlpha(float Alpha);
	UE_API void SetNeutralFrame(const TArray<float>& InNeutralFrame);
	
	UE_API bool ProcessFrame(const TArray<FName>& InPropertyNames, TArray<float>& InOutFrame) const;
	
private:

	static UE_API TArray<FName> DefaultProperties;
	
	TArray<FName> Properties;
	TArray<float> NeutralFrame;
	float Alpha = 1.0;

};

#undef UE_API

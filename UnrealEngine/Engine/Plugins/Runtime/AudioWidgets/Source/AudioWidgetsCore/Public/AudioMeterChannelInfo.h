// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AudioMeterChannelInfo.generated.h"

USTRUCT(BlueprintType)
struct FAudioMeterChannelInfo
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = AudioMeter)
	float MeterValue = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = AudioMeter)
	float PeakValue = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = AudioMeter)
	float ClippingValue = 0.0f;
};

inline bool operator==(const FAudioMeterChannelInfo& Lhs, const FAudioMeterChannelInfo& Rhs)
{
	return FMath::IsNearlyEqual(Lhs.MeterValue, Rhs.MeterValue) &&
		   FMath::IsNearlyEqual(Lhs.PeakValue, Rhs.PeakValue)   &&
		   FMath::IsNearlyEqual(Lhs.ClippingValue, Rhs.ClippingValue);
}

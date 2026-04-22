// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AVConfig.h"

#include "SimpleAV.generated.h"

#define UE_API AVCODECSCORERHI_API

UENUM(BlueprintType)
enum class ESimpleAVPreset : uint8
{
	Default,
	
	UltraLowQuality,
	LowQuality,
	HighQuality,
	Lossless,
};

UCLASS(MinimalAPI, Abstract)
class USimpleAVHelper : public UObject
{
	GENERATED_BODY()

public:
	static UE_API EAVPreset ConvertPreset(ESimpleAVPreset From);
};

#undef UE_API

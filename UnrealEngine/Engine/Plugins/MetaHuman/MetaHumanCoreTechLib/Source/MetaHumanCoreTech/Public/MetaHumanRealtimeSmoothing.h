// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "MetaHumanOneEuroFilter.h"

#include "Engine/DataAsset.h"

#include "MetaHumanRealtimeSmoothing.generated.h"

#define UE_API METAHUMANCORETECH_API



UENUM()
enum class EMetaHumanRealtimeSmoothingParamMethod : uint8
{
	RollingAverage = 0,
	OneEuro,
};
		
USTRUCT()
struct FMetaHumanRealtimeSmoothingParam 
{
public:

	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Data")
	EMetaHumanRealtimeSmoothingParamMethod Method = EMetaHumanRealtimeSmoothingParamMethod::RollingAverage;

	UPROPERTY(EditAnywhere, Category = "Data", DisplayName = "Number of frames", meta = (EditCondition = "Method == EMetaHumanRealtimeSmoothingParamMethod::RollingAverage", EditConditionHides))
	uint8 RollingAverageFrame = 1;

	UPROPERTY(EditAnywhere, Category = "Data", DisplayName = "Slope", meta = (EditCondition = "Method == EMetaHumanRealtimeSmoothingParamMethod::OneEuro", EditConditionHides))
	float OneEuroSlope = 5000;

	UPROPERTY(EditAnywhere, Category = "Data", DisplayName = "Min cutoff", meta = (EditCondition = "Method == EMetaHumanRealtimeSmoothingParamMethod::OneEuro", EditConditionHides))
	float OneEuroMinCutoff = 5;
};

UCLASS(MinimalAPI, DisplayName = "MetaHuman Realtime Smoothing")
class UMetaHumanRealtimeSmoothingParams : public UDataAsset
{
public:

	GENERATED_BODY()

	UE_API virtual void PostInitProperties() override;

	UPROPERTY(EditAnywhere, Category = "Smoothing")
	TMap<FName, FMetaHumanRealtimeSmoothingParam> Parameters;
};

class FMetaHumanRealtimeSmoothing
{

public:

	UE_API FMetaHumanRealtimeSmoothing(const TMap<FName, FMetaHumanRealtimeSmoothingParam>& InSmoothingParams);

	static UE_API TMap<FName, FMetaHumanRealtimeSmoothingParam> GetDefaultSmoothingParams();

	UE_API bool ProcessFrame(const TArray<FName>& InPropertyNames, TArray<float>& InOutFrame, double InDeltaTime);

private:

	TMap<FName, FMetaHumanRealtimeSmoothingParam> SmoothingParams;

	static constexpr uint8 DefaultRollingAverageFrameCount = 1;
	static UE_API TMap<FName, uint8> DefaultRollingAverage;
	static UE_API TMap<FName, TPair<float, float>> DefaultOneEuro;

	uint8 RollingAverageMaxBufferSize = 1;
	TArray<TArray<float>> RollingAverageBuffer;

	TMap<FName, FMetaHumanOneEuroFilter> OneEuroFilters;
	FMetaHumanOneEuroFilter OneEuroYAxis[3];
	FMetaHumanOneEuroFilter OneEuroXAxis[3];

	UE_API bool IsOrientation(const FName& InProperty) const;
};

#undef UE_API

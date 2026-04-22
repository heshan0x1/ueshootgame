// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DynamicWindSkeletalData.h"
#include "Engine/DataAsset.h"
#include "PVWindSettings.generated.h"

UCLASS(MinimalAPI, BlueprintType)
class UPVWindSettings : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category="Wind Settings")
	bool bOverwriteExisting = false;
	
	UPROPERTY(EditAnywhere, Category="Wind Settings")
	TArray<FDynamicWindSimulationGroupData> SimulationGroupData;
};
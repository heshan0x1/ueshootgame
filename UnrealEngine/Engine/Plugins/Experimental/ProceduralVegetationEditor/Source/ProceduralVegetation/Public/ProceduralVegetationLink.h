// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/AssetUserData.h"
#include "ProceduralVegetation.h"
#include "ProceduralVegetationLink.generated.h"

UCLASS(MinimalAPI)
class UProceduralVegetationLink : public UAssetUserData
{
	GENERATED_BODY()

public:
	UPROPERTY(VisibleAnywhere, Category = Settings)
	TObjectPtr<UProceduralVegetation> Source;
};

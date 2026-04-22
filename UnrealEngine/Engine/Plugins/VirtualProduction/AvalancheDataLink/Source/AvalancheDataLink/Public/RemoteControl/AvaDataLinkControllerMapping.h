// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AvaRCControllerId.h"
#include "AvaDataLinkControllerMapping.generated.h"

USTRUCT(BlueprintType, DisplayName="Motion Design Data Link RC Controller Mapping")
struct FAvaDataLinkControllerMapping
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motion Design Data Link")
	FString OutputFieldName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motion Design Data Link")
	FAvaRCControllerId TargetController;
};

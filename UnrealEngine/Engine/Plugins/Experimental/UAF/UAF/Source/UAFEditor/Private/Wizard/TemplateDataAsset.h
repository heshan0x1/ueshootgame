// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Styling/SlateBrush.h"

#include "TemplateDataAsset.generated.h"

UCLASS()
class UUAFTemplateDataAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = Default)
	FText Title;

	UPROPERTY(EditAnywhere, Category = Default)
	FText Description;
	
	UPROPERTY(EditAnywhere, Category = Default)
	FString DocumentationUrl;

	UPROPERTY(EditAnywhere, Category = Default)
	TArray<FText> Tags;

	UPROPERTY(EditAnywhere, Category = Default)
	FSlateBrush ThumbnailImage;

	UPROPERTY(EditAnywhere, Category = Default)
	FSlateBrush DetailsImage;

	UPROPERTY(EditAnywhere, Category = Default)
	TArray<TObjectPtr<UObject>> Assets;

	UPROPERTY(EditAnywhere, Category = Default)
	TObjectPtr<UObject> AssetToOpen;

	UPROPERTY(EditAnywhere, Category = Default)
	FString DefaultBlueprintAssetName;
};
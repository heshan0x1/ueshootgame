// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "InterchangeFilePickerBase.h"

#include "InterchangeOpenFileDialog.generated.h"

#define UE_API INTERCHANGEEDITORUTILITIES_API

UCLASS(MinimalAPI, BlueprintType, Blueprintable)
class UInterchangeFilePickerGeneric : public UInterchangeFilePickerBase
{
	GENERATED_BODY()

public:

protected:

	UE_API virtual bool FilePickerForTranslatorAssetType(const EInterchangeTranslatorAssetType TranslatorAssetType, FInterchangeFilePickerParameters& Parameters, TArray<FString>& OutFilenames) override;
	UE_API virtual bool FilePickerForTranslatorType(const EInterchangeTranslatorType TranslatorType, FInterchangeFilePickerParameters& Parameters, TArray<FString>& OutFilenames) override;
};

#undef UE_API

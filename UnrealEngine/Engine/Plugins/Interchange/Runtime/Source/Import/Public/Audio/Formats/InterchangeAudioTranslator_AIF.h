// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once
#include "Audio/Formats/InterchangeAudioTranslatorBase.h"

#include "InterchangeAudioTranslator_AIF.generated.h"

#define UE_API INTERCHANGEIMPORT_API

UCLASS(MinimalAPI, BlueprintType)
class UInterchangeAudioTranslator_AIF : public UInterchangeAudioTranslatorBase
{
	GENERATED_BODY()
public:
	UE_API virtual TArray<FString> GetSupportedFormats() const override;
};

#undef UE_API

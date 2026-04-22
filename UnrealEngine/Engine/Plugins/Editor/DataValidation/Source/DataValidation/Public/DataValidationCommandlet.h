// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Commandlets/Commandlet.h"
#include "DataValidationCommandlet.generated.h"

#define UE_API DATAVALIDATION_API

UCLASS(MinimalAPI, CustomConstructor)
class UDataValidationCommandlet : public UCommandlet
{
	GENERATED_UCLASS_BODY()

public:
	UDataValidationCommandlet(const FObjectInitializer& ObjectInitializer)
		: Super(ObjectInitializer)
	{
		LogToConsole = false;
	}

	// Begin UCommandlet Interface
	UE_API virtual int32 Main(const FString& FullCommandLine) override;
	// End UCommandlet Interface

	// do the validation without creating a commandlet
	static UE_API bool ValidateData(const FString& FullCommandLine);
};

#undef UE_API

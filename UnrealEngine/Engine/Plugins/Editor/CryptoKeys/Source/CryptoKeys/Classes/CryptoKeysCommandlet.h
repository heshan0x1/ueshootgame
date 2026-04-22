// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Commandlets/Commandlet.h"
#include "CryptoKeysCommandlet.generated.h"

#define UE_API CRYPTOKEYS_API

/**
* Commandlet used to configure project encryption settings
*/
UCLASS(MinimalAPI)
class UCryptoKeysCommandlet : public UCommandlet
{
	GENERATED_UCLASS_BODY()

		//~ Begin UCommandlet Interface
		UE_API virtual int32 Main(const FString& Params) override;
	//~ End UCommandlet Interface
};

#undef UE_API

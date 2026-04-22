// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Objects/USDSchemaTranslator.h"

#define UE_API USDSCHEMAS_API

#if USE_USD_SDK && WITH_EDITOR

class FUsdNaniteAssemblyTranslator : public FUsdSchemaTranslator
{
	friend class FUsdNaniteAssemblyCreateAssetsTaskChain;

public:
	using Super = FUsdSchemaTranslator;

	using FUsdSchemaTranslator::FUsdSchemaTranslator;

	FUsdNaniteAssemblyTranslator(const FUsdNaniteAssemblyTranslator& Other) = delete;
	FUsdNaniteAssemblyTranslator& operator=(const FUsdNaniteAssemblyTranslator& Other) = delete;

	UE_API virtual void CreateAssets() override;
};

#endif	  // #if USE_USD_SDK && WITH_EDITOR

#undef UE_API

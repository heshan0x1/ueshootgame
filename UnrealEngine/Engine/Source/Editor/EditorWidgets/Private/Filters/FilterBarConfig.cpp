// Copyright Epic Games, Inc. All Rights Reserved.

#include "Filters/FilterBarConfig.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(FilterBarConfig)

TObjectPtr<UFilterBarConfig> UFilterBarConfig::Instance = nullptr;

void UFilterBarConfig::Initialize()
{
	if(!Instance)
	{
		Instance = NewObject<UFilterBarConfig>(); 
		Instance->AddToRoot();
	}
}

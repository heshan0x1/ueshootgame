// Copyright Epic Games, Inc. All Rights Reserved.

#include "ContentBrowserConfig.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(ContentBrowserConfig)

TObjectPtr<UContentBrowserConfig> UContentBrowserConfig::Instance = nullptr;

void UContentBrowserConfig::Initialize()
{
	if (!Instance)
	{
		Instance = NewObject<UContentBrowserConfig>(); 
		Instance->AddToRoot();
	}
}

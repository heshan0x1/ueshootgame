// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldBrowserConfig.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(WorldBrowserConfig)

TObjectPtr<UWorldBrowserConfig> UWorldBrowserConfig::Instance = nullptr;

void UWorldBrowserConfig::Initialize()
{
	if(!Instance)
	{
		Instance = NewObject<UWorldBrowserConfig>(); 
		Instance->AddToRoot();
		Instance->LoadEditorConfig();
	}
}

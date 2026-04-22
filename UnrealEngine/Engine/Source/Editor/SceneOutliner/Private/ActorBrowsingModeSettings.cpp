// Copyright Epic Games, Inc. All Rights Reserved.

#include "ActorBrowsingModeSettings.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(ActorBrowsingModeSettings)

TObjectPtr<UActorBrowserConfig> UActorBrowserConfig::Instance = nullptr;

void UActorBrowserConfig::Initialize()
{
	if(!Instance)
	{
		Instance = NewObject<UActorBrowserConfig>(); 
		Instance->AddToRoot();
	}
}

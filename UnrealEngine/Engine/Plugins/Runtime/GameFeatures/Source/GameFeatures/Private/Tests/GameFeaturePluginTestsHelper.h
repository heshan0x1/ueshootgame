// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameFeaturesSubsystem.h"

enum class EShouldActivate : uint8
{
	No,
	Yes
};

struct FGameFeatureDependsProperties
{
	FString PluginName;
	EShouldActivate ShouldActivate = EShouldActivate::No;
};

struct FGameFeatureProperties
{
	FString PluginName;
	EGameFeatureTargetState	BuiltinAutoState = EGameFeatureTargetState::Installed;
	TArray<FGameFeatureDependsProperties> Depends;
};

bool CreateGameFeaturePlugin(FGameFeatureProperties Properties, FString& OutPluginURL);

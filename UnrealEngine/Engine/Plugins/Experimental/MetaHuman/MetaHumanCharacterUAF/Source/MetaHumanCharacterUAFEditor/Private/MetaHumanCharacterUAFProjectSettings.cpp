// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanCharacterUAFProjectSettings.h"
#include "MetaHumanTypes.h"

UMetaHumanCharacterUAFProjectSettings::UMetaHumanCharacterUAFProjectSettings()
{
	// Add the slots for all possible legacy pipelines to be configured in the project settings.
	Blueprints.Add(EMetaHumanQualityLevel::Cinematic);
	Blueprints.Add(EMetaHumanQualityLevel::High);
	Blueprints.Add(EMetaHumanQualityLevel::Medium);
	Blueprints.Add(EMetaHumanQualityLevel::Low);
}
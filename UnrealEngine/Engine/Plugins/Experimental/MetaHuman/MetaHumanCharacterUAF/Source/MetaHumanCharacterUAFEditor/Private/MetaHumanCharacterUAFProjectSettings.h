// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/DeveloperSettings.h"
#include "UObject/SoftObjectPtr.h"
#include "MetaHumanCharacterPaletteProjectSettings.h"

#include "MetaHumanCharacterUAFProjectSettings.generated.h"

UCLASS(Config = MetaHumanCharacterUAF, DefaultConfig)
class UMetaHumanCharacterUAFProjectSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UMetaHumanCharacterUAFProjectSettings();

	// Begin UDeveloperSettings interface
	virtual FName GetCategoryName() const override { return FName("Plugins"); }
	virtual FName GetSectionName() const override { return FName("MetaHumanCharacterUAF"); }
#if WITH_EDITOR
	virtual FText GetSectionText() const override { return NSLOCTEXT("MetaHumanCharacterProjectSettings", "SettingsName", "MetaHuman Character UAF"); }
	virtual FText GetSectionDescription() const override { return NSLOCTEXT("MetaHumanCharacterProjectSettingsDesc", "SettingsDescription", "Configuration settings for MetaHuman Character UAF build features"); }
#endif // WITH_EDITOR
	// End UDeveloperSettings interface

	/** Default actor blueprints to use when assembling a MetaHuman for UAF. */
	UPROPERTY(Config, EditAnywhere, Category = "Build", EditFixedSize, NoClear)
	TMap<EMetaHumanQualityLevel, TSoftClassPtr<AActor>> Blueprints;
};

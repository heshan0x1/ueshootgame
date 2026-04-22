// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "HomeScreenCommon.h"
#include "UObject/Object.h"
#include "HomeScreenSettings.generated.h"

DECLARE_MULTICAST_DELEGATE_OneParam(FOnLoadAtStartupChanged, EAutoLoadProject)

// Load on Startup setting is registered with the FEditorLoadingSavingSettingsCustomization in the Loading & Saving Section

UCLASS(MinimalAPI, config=EditorSettings)
class UHomeScreenSettings : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY()
	EAutoLoadProject LoadAtStartup = EAutoLoadProject::HomeScreen;

public:
	MAINFRAME_API virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	MAINFRAME_API virtual void PostInitProperties() override;

	FOnLoadAtStartupChanged& OnLoadAtStartupChanged() { return OnLoadAtStartupChangedDelegate; }

private:
	void OnMostRecentProjectEditorSettingChanged(bool bInAutoLoad);

private:
	FOnLoadAtStartupChanged OnLoadAtStartupChangedDelegate;
};

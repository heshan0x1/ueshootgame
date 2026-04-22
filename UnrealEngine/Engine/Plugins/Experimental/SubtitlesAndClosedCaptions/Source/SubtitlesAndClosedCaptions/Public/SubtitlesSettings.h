// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/DeveloperSettings.h"
#include "Math/Color.h"
#include "SubtitleWidget.h"
#include "SubtitlesSettings.generated.h"


UCLASS(config = Game, defaultconfig, meta = (DisplayName = "Subtitles And Closed Captions"))
class USubtitlesSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	USubtitlesSettings();

	const TSubclassOf<USubtitleWidget>& GetWidget() const { return SubtitleWidgetToUse; }
	const TSubclassOf<USubtitleWidget>& GetWidgetDefault() const { return SubtitleWidgetToUseDefault; }

protected:

	UPROPERTY(config, EditDefaultsOnly, Category = Subtitles, AdvancedDisplay)
	TSubclassOf<USubtitleWidget> SubtitleWidgetToUse;

	UPROPERTY(config)
	TSubclassOf<USubtitleWidget> SubtitleWidgetToUseDefault; // fallback for SubtitleWidgetToUse (not set by user)
};

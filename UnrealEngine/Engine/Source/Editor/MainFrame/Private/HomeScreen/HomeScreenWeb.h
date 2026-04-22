// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "Settings/HomeScreenCommon.h"
#include "UObject/Object.h"
#include "HomeScreenWeb.generated.h"

DECLARE_MULTICAST_DELEGATE_OneParam(FOnNavigationChanged, const EMainSectionMenu);
DECLARE_MULTICAST_DELEGATE(FOnGettingStartedProjectRequested);

UCLASS()
class UHomeScreenWeb : public UObject
{
	GENERATED_BODY()

public:
	UFUNCTION()
	void NavigateTo(EMainSectionMenu InSectionToNavigate);
	FOnNavigationChanged& OnNavigationChanged() { return OnNavigationChangedDelegate; }

	UFUNCTION()
	void OpenGettingStartedProject();
	FOnGettingStartedProjectRequested& OnTutorialProjectRequested() { return OnTutorialProjectRequestedDelegate; }

	UFUNCTION()
	void OpenWebPage(const FString& InURL) const;

private:
	FOnNavigationChanged OnNavigationChangedDelegate;
	FOnGettingStartedProjectRequested OnTutorialProjectRequestedDelegate;
	EMainSectionMenu SectionToNavigate = EMainSectionMenu::None;
};

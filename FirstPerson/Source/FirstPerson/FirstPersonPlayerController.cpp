// Copyright Epic Games, Inc. All Rights Reserved.


#include "FirstPersonPlayerController.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "InputMappingContext.h"
#include "FirstPersonCameraManager.h"
#include "Blueprint/UserWidget.h"
#include "FirstPerson.h"
#include "Widgets/Input/SVirtualJoystick.h"

AFirstPersonPlayerController::AFirstPersonPlayerController()
{
	// set the player camera manager class
	PlayerCameraManagerClass = AFirstPersonCameraManager::StaticClass();
}

void AFirstPersonPlayerController::BeginPlay()
{
	Super::BeginPlay();
	UE_LOG(LogFirstPerson, Log, TEXT("AFirstPersonPlayerController::BeginPlay. GetNetMode:%d"), GetNetMode());

	// only spawn touch controls on local player controllers
	if (GetNetMode() != NM_DedicatedServer && ShouldUseTouchControls() && IsLocalPlayerController())
	{
		// spawn the mobile controls widget
		MobileControlsWidget = CreateWidget<UUserWidget>(this, MobileControlsWidgetClass);

		if (MobileControlsWidget)
		{
			// add the controls to the player screen
			MobileControlsWidget->AddToPlayerScreen(0);

		} else {

			UE_LOG(LogFirstPerson, Error, TEXT("Could not spawn mobile controls widget."));

		}

	}
}

void AFirstPersonPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	// only add IMCs for local player controllers
	if (IsLocalPlayerController())
	{
		// Add Input Mapping Context
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
		{
			for (UInputMappingContext* CurrentContext : DefaultMappingContexts)
			{
				Subsystem->AddMappingContext(CurrentContext, 0);
			}

			// only add these IMCs if we're not using mobile touch input
			if (!ShouldUseTouchControls())
			{
				for (UInputMappingContext* CurrentContext : MobileExcludedMappingContexts)
				{
					Subsystem->AddMappingContext(CurrentContext, 0);
				}
			}
		}
	}
	
}

bool AFirstPersonPlayerController::ShouldUseTouchControls() const
{
	// are we on a mobile platform? Should we force touch?
	return SVirtualJoystick::ShouldDisplayTouchInterface() || bForceTouchControls;
}

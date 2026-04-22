// Copyright Epic Games, Inc. All Rights Reserved.

#include "ViewportInteractions/ViewportInteractionsUtilities.h"

#include "ViewportInteractions/ViewportCameraSpeedMouseWheelInteraction.h"
#include "ViewportInteractions/ViewportCameraRotateInteraction.h"
#include "ViewportInteractions/ViewportCameraTranslateInteraction.h"
#include "ViewportInteractions/ViewportFOVInteraction.h"
#include "ViewportInteractions/ViewportInteractionsBehaviorSource.h"
#include "ViewportInteractions/ViewportMoveYawInteraction.h"
#include "ViewportInteractions/ViewportOrbitInteraction.h"
#include "ViewportInteractions/ViewportOrthoPanInteraction.h"
#include "ViewportInteractions/ViewportPanInteraction.h"
#include "ViewportInteractions/ViewportViewAngleInteraction.h"
#include "ViewportInteractions/ViewportZoomInteraction.h"

void UE::Editor::ViewportInteractions::AddDefaultCameraMovementInteractions(
	UViewportInteractionsBehaviorSource* InInteractionsBehaviorSource
)
{
	if (InInteractionsBehaviorSource)
	{
		InInteractionsBehaviorSource->AddInteractions(
			{UViewportCameraSpeedMouseWheelInteraction::StaticClass(),
				UViewportOrbitInteraction::StaticClass(),
				UViewportMoveYawInteraction::StaticClass(),
				UViewportViewAngleInteraction::StaticClass(),
				UViewportOrthoPanInteraction::StaticClass(),
				UViewportPanInteraction::StaticClass(),
				UViewportZoomInteraction::StaticClass(),
				UViewportCameraTranslateInteraction::StaticClass(),
				UViewportCameraRotateInteraction::StaticClass(),
				UViewportFOVInteraction::StaticClass() }
		);
	}
}

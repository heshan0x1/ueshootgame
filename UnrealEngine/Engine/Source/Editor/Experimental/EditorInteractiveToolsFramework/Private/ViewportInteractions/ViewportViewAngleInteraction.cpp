// Copyright Epic Games, Inc. All Rights Reserved.

#include "ViewportInteractions/ViewportViewAngleInteraction.h"
#include "BaseBehaviors/ClickDragBehavior.h"
#include "BaseBehaviors/SingleClickOrDragBehavior.h"
#include "EditorViewportClient.h"
#include "Settings/LevelEditorViewportSettings.h"
#include "ViewportClientNavigationHelper.h"
#include "ViewportInteractions/ViewportInteractionsBehaviorSource.h"

UViewportViewAngleInteraction::UViewportViewAngleInteraction()
{
	ToolType = UE::Editor::ViewportInteractions::CameraDrag;

	InteractionName = TEXT("View Angle");

	SetClickOrDragPriority(UE::Editor::ViewportInteractions::VIEWPORT_INTERACTIONS_AFTER_GIZMOS_PRIORITY);
}

FInputRayHit UViewportViewAngleInteraction::CanBeginClickDragSequence(const FInputDeviceRay& InPressPos)
{
	bool bSuccess = CanBeActivated();
	if (FEditorViewportClient* EditorViewportClient = GetEditorViewportClient())
	{
		bSuccess &= EditorViewportClient->IsPerspective();
	}

	return bSuccess ? FInputRayHit(TNumericLimits<float>::Max()) : FInputRayHit();
}

void UViewportViewAngleInteraction::OnClickPress(const FInputDeviceRay& PressPos)
{
	UE::Editor::ViewportInteractions::LOG(TEXT("VIEW ANGLE 1"));

	UViewportDragInteraction::OnClickPress(PressPos);

	if (UViewportInteractionsBehaviorSource* BehaviorSource = GetViewportInteractionsBehaviorSource())
	{
		BehaviorSource->SetIsMouseLooking(true);
	}
}

void UViewportViewAngleInteraction::OnClickRelease(const FInputDeviceRay& ReleasePos)
{
	UE::Editor::ViewportInteractions::LOG(TEXT("VIEW ANGLE 0"));

	UViewportDragInteraction::OnClickRelease(ReleasePos);

	if (UViewportInteractionsBehaviorSource* BehaviorSource = GetViewportInteractionsBehaviorSource())
	{
		BehaviorSource->SetIsMouseLooking(false);
	}
}

void UViewportViewAngleInteraction::OnDragDelta(float InMouseDeltaX, float InMouseDeltaY)
{
	if (FEditorViewportClient* const EditorViewportClient = GetEditorViewportClient())
	{
		// Change viewing angle

		// Inverting orbit axis is handled elsewhere
		const bool bInvertY = !EditorViewportClient->ShouldOrbitCamera()
						   && GetDefault<ULevelEditorViewportSettings>()->bInvertMouseLookYAxis;
		float Direction = bInvertY ? -1.0f : 1.0f;

		const ULevelEditorViewportSettings* ViewportSettings = GetDefault<ULevelEditorViewportSettings>();

		FRotator RotDelta;
		RotDelta.Yaw = InMouseDeltaX * ViewportSettings->MouseSensitivty;
		RotDelta.Pitch = -InMouseDeltaY * ViewportSettings->MouseSensitivty * Direction;
		RotDelta.Roll = 0.0f;

		FRotator& ViewportRotationDelta = EditorViewportClient->GetViewportNavigationHelper()->RotationDelta;
		ViewportRotationDelta += RotDelta;
	}
}

FDeviceButtonState UViewportViewAngleInteraction::GetActiveMouseButtonState(const FInputDeviceState& Input)
{
	return Input.Mouse.Right;
}

void UViewportViewAngleInteraction::OnUpdateModifierState(int ModifierID, bool bIsOn)
{
	Super::OnUpdateModifierState(ModifierID, bIsOn);
}

// Copyright Epic Games, Inc. All Rights Reserved.

#include "ViewportInteractions/ViewportMoveYawInteraction.h"
#include "BaseBehaviors/ClickDragBehavior.h"
#include "BaseBehaviors/SingleClickOrDragBehavior.h"
#include "EditorViewportClient.h"
#include "Settings/LevelEditorViewportSettings.h"
#include "ViewportClientNavigationHelper.h"
#include "EditorDragTools/DragToolsBehaviourSource.h"
#include "ViewportInteractions/ViewportInteractionsBehaviorSource.h"

UViewportMoveYawInteraction::UViewportMoveYawInteraction()
{
	ToolType = UE::Editor::ViewportInteractions::CameraDrag;

	InteractionName = TEXT("Move-Yaw");

	SetClickOrDragPriority(FInputCapturePriority::DEFAULT_GIZMO_PRIORITY + 1);
}

FInputRayHit UViewportMoveYawInteraction::CanBeginClickDragSequence(const FInputDeviceRay& InPressPos)
{
	bool bSuccess = CanBeActivated();
	if (FEditorViewportClient* EditorViewportClient = GetEditorViewportClient())
	{
		bSuccess &= EditorViewportClient->IsPerspective();
	}

	return bSuccess ? FInputRayHit(TNumericLimits<float>::Max()) : FInputRayHit();
}

void UViewportMoveYawInteraction::OnClickPress(const FInputDeviceRay& PressPos)
{
	UE::Editor::ViewportInteractions::LOG(TEXT("MOVE YAW 1"));

	UViewportDragInteraction::OnClickPress(PressPos);

	if (UViewportInteractionsBehaviorSource* BehaviorSource = GetViewportInteractionsBehaviorSource())
	{
		BehaviorSource->SetIsMouseLooking(true);
	}
}

void UViewportMoveYawInteraction::OnClickRelease(const FInputDeviceRay& ReleasePos)
{
	UE::Editor::ViewportInteractions::LOG(TEXT("MOVE YAW 0"));

	UViewportDragInteraction::OnClickRelease(ReleasePos);

	if (UViewportInteractionsBehaviorSource* BehaviorSource = GetViewportInteractionsBehaviorSource())
	{
		BehaviorSource->SetIsMouseLooking(false);
	}
}

void UViewportMoveYawInteraction::OnDragDelta(float InMouseDeltaX, float InMouseDeltaY)
{
	if (FEditorViewportClient* const EditorViewportClient = GetEditorViewportClient())
	{
		FVector LocationDelta = FVector::ZeroVector;
		FRotator RotDelta = FRotator::ZeroRotator;

		LocationDelta.X = -InMouseDeltaY * FMath::Cos(EditorViewportClient->GetViewRotation().Yaw * PI / 180.f);
		LocationDelta.Y = -InMouseDeltaY * FMath::Sin(EditorViewportClient->GetViewRotation().Yaw * PI / 180.f);

		const ULevelEditorViewportSettings* ViewportSettings = GetDefault<ULevelEditorViewportSettings>();
		RotDelta.Yaw = InMouseDeltaX * ViewportSettings->MouseSensitivty;

		FVector& ViewportLocationDelta = EditorViewportClient->GetViewportNavigationHelper()->LocationDelta;
		ViewportLocationDelta += LocationDelta;

		FRotator& ViewportRotationDelta = EditorViewportClient->GetViewportNavigationHelper()->RotationDelta;
		ViewportRotationDelta += RotDelta;
	}
}

FDeviceButtonState UViewportMoveYawInteraction::GetActiveMouseButtonState(const FInputDeviceState& Input)
{
	return Super::GetActiveMouseButtonState(Input);
}

bool UViewportMoveYawInteraction::CanBeActivated(const FInputDeviceState& InInputDeviceState) const
{
	if (const FEditorViewportClient* const EditorViewportClient = GetEditorViewportClient())
	{
		return EditorViewportClient->IsPerspective();
	}

	return false;
}

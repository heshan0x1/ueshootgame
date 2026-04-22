// Copyright Epic Games, Inc. All Rights Reserved.

#include "ViewportInteractions/ViewportOrbitInteraction.h"
#include "BaseBehaviors/ClickDragBehavior.h"
#include "EditorViewportClient.h"
#include "ViewportClientNavigationHelper.h"
#include "BaseBehaviors/SingleClickOrDragBehavior.h"
#include "ViewportInteractions/ViewportInteractionsBehaviorSource.h"

UViewportOrbitInteraction::UViewportOrbitInteraction()
{
	ToolType = UE::Editor::ViewportInteractions::Orbit;

	InteractionName = TEXT("Orbit");

	if (TStrongObjectPtr<USingleClickOrDragInputBehavior> ClickOrDragBehavior = ClickOrDragInputBehavior.Pin())
	{
		// Priority lower than gizmos, so that we can still Alt - Duplicate.
		// This might need adjustments once we have all the pieces together (gizmo, drag tools, viewport interactions)
		ClickOrDragBehavior->SetDefaultPriority(FInputCapturePriority::DEFAULT_GIZMO_PRIORITY + 1);
	}
}

FInputRayHit UViewportOrbitInteraction::CanBeginClickDragSequence(const FInputDeviceRay& InPressPos)
{
	if (CanBeActivated())
	{
		if (FEditorViewportClient* EditorViewportClient = GetEditorViewportClient())
		{
			return EditorViewportClient->IsPerspective() ? FInputRayHit(TNumericLimits<float>::Max()) : FInputRayHit();
		}
	}

	return FInputRayHit();
}

void UViewportOrbitInteraction::OnClickPress(const FInputDeviceRay& InPressPos)
{
	UE::Editor::ViewportInteractions::LOG(TEXT("ORBIT 1"));

	if (FEditorViewportClient* EditorViewportClient = GetEditorViewportClient())
	{
		EditorViewportClient->ToggleOrbitCamera(true);
	}

	if (UViewportInteractionsBehaviorSource* BehaviorSource = GetViewportInteractionsBehaviorSource())
	{
		BehaviorSource->SetIsMouseLooking(true);
	}

	UViewportDragInteraction::OnClickPress(InPressPos);
}

void UViewportOrbitInteraction::OnClickRelease(const FInputDeviceRay& InReleasePos)
{
	UE::Editor::ViewportInteractions::LOG(TEXT("ORBIT 0"));

	if (FEditorViewportClient* EditorViewportClient = GetEditorViewportClient())
	{
		EditorViewportClient->ToggleOrbitCamera(false);
	}

	if (UViewportInteractionsBehaviorSource* BehaviorSource = GetViewportInteractionsBehaviorSource())
	{
		BehaviorSource->SetIsMouseLooking(false);
	}

	UViewportDragInteraction::OnClickRelease(InReleasePos);
}

void UViewportOrbitInteraction::OnDragDelta(float InMouseDeltaX, float InMouseDeltaY)
{
	if (FEditorViewportClient* const EditorViewportClient = GetEditorViewportClient())
	{
		constexpr bool bNudge = false;

		EditorViewportClient->SetCurrentWidgetAxis(EAxisList::None);

		// TODO: we can probably extract just the part dedicated to perspective view from EditorViewportClient->TranslateDelta
		FVector Delta = FVector::ZeroVector;
		Delta += EditorViewportClient->TranslateDelta(EKeys::MouseX, InMouseDeltaX, bNudge);
		Delta += EditorViewportClient->TranslateDelta(EKeys::MouseY, -InMouseDeltaY, bNudge);

		FVector& ViewportOrbitDelta = EditorViewportClient->GetViewportNavigationHelper()->OrbitDelta;
		ViewportOrbitDelta += Delta;
	}
}

bool UViewportOrbitInteraction::CanBeActivated(const FInputDeviceState& InInputDeviceState) const
{
	return IsEnabled() && IsAltDown();
}

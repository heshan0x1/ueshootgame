// Copyright Epic Games, Inc. All Rights Reserved.

#include "ViewportInteractions/ViewportOrthoPanInteraction.h"
#include "BaseBehaviors/ClickDragBehavior.h"
#include "EditorViewportClient.h"
#include "ViewportClientNavigationHelper.h"
#include "ViewportInteractions/ViewportInteractionsBehaviorSource.h"

UViewportOrthoPanInteraction::UViewportOrthoPanInteraction()
{
	ToolType = UE::Editor::ViewportInteractions::CameraDrag;

	InteractionName = TEXT("Orthographic Panning");
}

FInputRayHit UViewportOrthoPanInteraction::CanBeginClickDragSequence(const FInputDeviceRay& InPressPos)
{
	bool bSuccess = CanBeActivated();
	if (FEditorViewportClient* EditorViewportClient = GetEditorViewportClient())
	{
		bSuccess &= EditorViewportClient->IsOrtho();
	}

	return bSuccess ? FInputRayHit(TNumericLimits<float>::Max()) : FInputRayHit();
}

void UViewportOrthoPanInteraction::OnClickPress(const FInputDeviceRay& PressPos)
{
	UE::Editor::ViewportInteractions::LOG(TEXT("ORTHO PAN 1"));

	UViewportDragInteraction::OnClickPress(PressPos);

	if (UViewportInteractionsBehaviorSource* BehaviorSource = GetViewportInteractionsBehaviorSource())
	{
		GetViewportInteractionsBehaviorSource()->SetMouseCursorOverride(EMouseCursor::Type::GrabHandClosed);
		BehaviorSource->SetIsMouseLooking(true);
	}
}

void UViewportOrthoPanInteraction::OnClickRelease(const FInputDeviceRay& ReleasePos)
{
	UE::Editor::ViewportInteractions::LOG(TEXT("ORTHO PAN 0"));

	UViewportDragInteraction::OnClickRelease(ReleasePos);

	if (UViewportInteractionsBehaviorSource* BehaviorSource = GetViewportInteractionsBehaviorSource())
	{
		BehaviorSource->SetIsMouseLooking(false);
	}
}

void UViewportOrthoPanInteraction::OnDragDelta(float InMouseDeltaX, float InMouseDeltaY)
{
	if (FEditorViewportClient* const EditorViewportClient = GetEditorViewportClient())
	{
		// based off FEditorViewportClient::TranslateDelta

		const float UnitsPerPixel = EditorViewportClient->GetOrthoUnitsPerPixel(EditorViewportClient->Viewport);
		FVector LocationDelta(-InMouseDeltaX * UnitsPerPixel, InMouseDeltaY * UnitsPerPixel, 0.0f);

		switch (EditorViewportClient->GetViewportType())
		{
		case LVT_OrthoTop:
			LocationDelta = FVector(-InMouseDeltaY * UnitsPerPixel, InMouseDeltaX * UnitsPerPixel, 0.0f);
			break;
		case LVT_OrthoLeft:
			LocationDelta = FVector(-InMouseDeltaX * UnitsPerPixel, 0.0f, InMouseDeltaY * UnitsPerPixel);
			break;
		case LVT_OrthoBack:
			LocationDelta = FVector(0.0f, -InMouseDeltaX * UnitsPerPixel, InMouseDeltaY * UnitsPerPixel);
			break;
		case LVT_OrthoBottom:
			LocationDelta = FVector(-InMouseDeltaY * UnitsPerPixel, -InMouseDeltaX * UnitsPerPixel, 0.0f);
			break;
		case LVT_OrthoRight:
			LocationDelta = FVector(InMouseDeltaX * UnitsPerPixel, 0.0f, InMouseDeltaY * UnitsPerPixel);
			break;
		case LVT_OrthoFront:
			LocationDelta = FVector(0.0f, InMouseDeltaX * UnitsPerPixel, InMouseDeltaY * UnitsPerPixel);
			break;
		case LVT_OrthoFreelook:
		case LVT_Perspective:
			break;
		}

		// Invert when Alt or Shift are down
		if (IsAltDown() || IsShiftDown())
		{
			LocationDelta *= -1.0f;
		}

		FVector& ViewportLocationDelta = EditorViewportClient->GetViewportNavigationHelper()->LocationDelta;
		ViewportLocationDelta += LocationDelta;
	}
}

FDeviceButtonState UViewportOrthoPanInteraction::GetActiveMouseButtonState(const FInputDeviceState& Input)
{
	return Input.Mouse.Right;
}
bool UViewportOrthoPanInteraction::CanBeActivated(const FInputDeviceState& InInputDeviceState) const
{
	//TODO: we might be able to remove the check for LMB
	return IsEnabled() && !IsLeftMouseButtonDown();
}

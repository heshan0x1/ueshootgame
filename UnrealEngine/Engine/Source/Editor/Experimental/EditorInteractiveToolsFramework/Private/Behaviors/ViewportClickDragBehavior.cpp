// Copyright Epic Games, Inc. All Rights Reserved.

#include "Behaviors/ViewportClickDragBehavior.h"

void UViewportClickDragBehavior::SetDragConfirmedPriority(FInputCapturePriority InDragConfirmedPriority)
{
	DragConfirmedPriority = InDragConfirmedPriority;
}

FInputCapturePriority UViewportClickDragBehavior::GetPriority()
{
	if (MouseTraveledDistance > ClickDistanceThreshold)
	{
		return DragConfirmedPriority;
	}

	return Super::GetPriority();
}

FInputCaptureRequest UViewportClickDragBehavior::WantsCapture(const FInputDeviceState& InInputState)
{
	bInClickDrag = false;	// should never be true here, but weird things can happen w/ focus

	const bool bIsPressed = IsAnyButtonPressed(InInputState);

	if (bIsPressed)
	{
		// Traveled distance is 0.0f if mouse was just pressed
		MouseTraveledDistance = 0.0f;

		// Cache button down location
		MouseDownPosition = InInputState.Mouse.Position2D;
	}
	else
	{
		MouseTraveledDistance = static_cast<float>(FVector2D::Distance(InInputState.Mouse.Position2D, MouseDownPosition));
	}

	bool bAnyButtonDown = IsAnyButtonDown(InInputState);

	// When stealing, we check button down state and traveled distance
	bool bIsStealing = MouseTraveledDistance > ClickDistanceThreshold && bAnyButtonDown;

	if ((bAnyButtonDown || bIsStealing) && (ModifierCheckFunc == nullptr || ModifierCheckFunc(InInputState)) )
	{
		const FInputRayHit HitResult = CanBeginClickDragSequence(GetDeviceRay(InInputState));
		if (HitResult.bHit)
		{
			return FInputCaptureRequest::Begin(this, EInputCaptureSide::Any, HitResult.HitDepth);
		}
	}
	return FInputCaptureRequest::Ignore();
}

bool UViewportClickDragBehavior::IsAnyButtonPressed(const FInputDeviceState& InInput)
{
	if (InInput.IsFromDevice(EInputDevices::Mouse)) 
	{
		ActiveDevice = EInputDevices::Mouse;

		const FDeviceButtonState& Left = InInput.Mouse.Left;
		const FDeviceButtonState& Middle = InInput.Mouse.Middle;
		const FDeviceButtonState& Right = InInput.Mouse.Right;

		const bool bLeftDown = HandlesLeftMouseButton() ? Left.bPressed : false;
		const bool bMiddleDown = HandlesMiddleMouseButton() ? Middle.bPressed : false;
		const bool bRightDown = HandlesRightMouseButton() ? Right.bPressed : false;

		return bLeftDown || bMiddleDown || bRightDown;
	}

	return false;
}

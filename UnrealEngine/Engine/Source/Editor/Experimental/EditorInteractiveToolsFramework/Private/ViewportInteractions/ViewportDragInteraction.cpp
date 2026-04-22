// Copyright Epic Games, Inc. All Rights Reserved.

#include "ViewportInteractions/ViewportDragInteraction.h"
#include "BaseBehaviors/ClickDragBehavior.h"
#include "BaseBehaviors/SingleClickOrDragBehavior.h"
#include "ViewportInteractions/ViewportInteractionsBehaviorSource.h"

UViewportDragInteraction::UViewportDragInteraction()
{
	bIsDragging = false;
	MouseX = 0;
	MouseY = 0;

	USingleClickOrDragInputBehavior* ClickDragBehavior = NewObject<USingleClickOrDragInputBehavior>();

	// Click Target can be specified separately if needed
	ClickDragBehavior->Initialize(nullptr, this);
	ClickDragBehavior->SetDefaultPriority(UE::Editor::ViewportInteractions::VIEWPORT_INTERACTIONS_DEFAULT_PRIORITY);

	ClickOrDragInputBehavior = ClickDragBehavior;

	RegisterInputBehavior(ClickDragBehavior);
}

FInputRayHit UViewportDragInteraction::CanBeginClickDragSequence(const FInputDeviceRay& InPressPos)
{
	return FInputRayHit();
}

void UViewportDragInteraction::OnClickPress(const FInputDeviceRay& InPressPos)
{
	if (!IsEnabled())
	{
		bIsDragging = false;
		return;
	}

	MouseX = static_cast<int32>(InPressPos.ScreenPosition.X);
	MouseY = static_cast<int32>(InPressPos.ScreenPosition.Y);

	bIsDragging = true;

	GetViewportInteractionsBehaviorSource()->SetMouseCursorOverride(EMouseCursor::None);
}

void UViewportDragInteraction::OnClickDrag(const FInputDeviceRay& InDragPos)
{
	if (!IsEnabled())
	{
		return;
	}

	const int32 ScreenPosX = static_cast<int32>(InDragPos.ScreenPosition.X);
	const int32 ScreenPosY = static_cast<int32>(InDragPos.ScreenPosition.Y);

	const int32 MouseDeltaX = ScreenPosX - MouseX;
	const int32 MouseDeltaY = ScreenPosY - MouseY;

	MouseX = ScreenPosX;
	MouseY = ScreenPosY;

	OnDragDelta(static_cast<float>(MouseDeltaX), static_cast<float>(MouseDeltaY));
}

void UViewportDragInteraction::OnClickRelease(const FInputDeviceRay& InReleasePos)
{
	MouseX = static_cast<int32>(InReleasePos.ScreenPosition.X);
	MouseY = static_cast<int32>(InReleasePos.ScreenPosition.Y);

	bIsDragging = false;
}

void UViewportDragInteraction::OnForceEndCapture()
{
	OnClickRelease(FInputDeviceRay());
}

void UViewportDragInteraction::Initialize(UViewportInteractionsBehaviorSource* const InViewportInteractionsBehaviorSource)
{
	UViewportInteraction::Initialize(InViewportInteractionsBehaviorSource);

	if (ClickOrDragInputBehavior.IsValid())
	{
		ClickOrDragInputBehavior->SetUseCustomMouseButton([this](const FInputDeviceState& InInputDeviceState)
		{
			return GetActiveMouseButtonState(InInputDeviceState);
		});

		ClickOrDragInputBehavior->ModifierCheckFunc = [this](const FInputDeviceState& InInputDeviceState)
		{
			return CanBeActivated(InInputDeviceState);
		};
	}
}

void UViewportDragInteraction::SetClickTarget(IClickBehaviorTarget* InClickBehaviorTarget)
{
	if (TStrongObjectPtr<USingleClickOrDragInputBehavior> ClickOrDragBehavior = ClickOrDragInputBehavior.Pin())
	{
		ClickOrDragBehavior->Initialize(InClickBehaviorTarget, this);
	}
}

void UViewportDragInteraction::SetClickOrDragPriority(int InPriority) const
{
	if (TStrongObjectPtr<USingleClickOrDragInputBehavior> ClickOrDragBehavior = ClickOrDragInputBehavior.Pin())
	{
		ClickOrDragBehavior->SetDefaultPriority(InPriority);
	}
}

FDeviceButtonState UViewportDragInteraction::GetActiveMouseButtonState(const FInputDeviceState& Input)
{
	return Input.Mouse.Left;
}

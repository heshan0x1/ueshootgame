// Copyright Epic Games, Inc. All Rights Reserved.

#include "ViewportInteractions/ViewportPanInteraction.h"
#include "BaseBehaviors/ClickDragBehavior.h"
#include "Behaviors/MultiButtonClickDragBehavior.h"
#include "EditorViewportClient.h"
#include "Settings/LevelEditorViewportSettings.h"
#include "ViewportClientNavigationHelper.h"
#include "ViewportInteractions/ViewportInteractionsBehaviorSource.h"

UViewportPanInteraction::UViewportPanInteraction()
{
	ToolType = UE::Editor::ViewportInteractions::CameraDrag;

	InteractionName = TEXT("Perspective Panning");

	// LMB + RMB + Drag to Pan
	UMultiButtonClickDragBehavior* MultiButtonClickDragBehavior = NewObject<UMultiButtonClickDragBehavior>();
	MultiButtonClickDragBehavior->Initialize();
	MultiButtonClickDragBehavior->EnableButton(EKeys::LeftMouseButton);
	MultiButtonClickDragBehavior->EnableButton(EKeys::RightMouseButton);

	MultiButtonClickDragBehavior->CanBeginClickDragFunc = [this](const FInputDeviceRay& InPressPos)
	{
		bool bSuccess = CanBeActivated();
		if (FEditorViewportClient* EditorViewportClient = GetEditorViewportClient())
		{
			bSuccess &= EditorViewportClient->IsPerspective();
		}

		return bSuccess ? FInputRayHit(TNumericLimits<float>::Max()) : FInputRayHit();
	};

	MultiButtonClickDragBehavior->ModifierCheckFunc = [this](const FInputDeviceState& InInputDeviceState)
	{
		return InInputDeviceState.Mouse.Left.bDown && InInputDeviceState.Mouse.Right.bDown;
	};

	MultiButtonClickDragBehavior->OnClickPressFunc = [this](const FInputDeviceRay& InPressPos)
	{
		OnClickPress(InPressPos);
		bMultiButtonPanInteraction = true;
	};

	MultiButtonClickDragBehavior->OnClickReleaseFunc = [this](const FInputDeviceRay& InReleasePos)
	{
		OnClickRelease(InReleasePos);
	};

	MultiButtonClickDragBehavior->OnClickDragFunc = [this](const FInputDeviceRay& InDragPos)
	{
		OnClickDrag(InDragPos);
	};

	MultiButtonClickDragBehavior->SetDefaultPriority(UE::Editor::ViewportInteractions::VIEWPORT_INTERACTIONS_HIGH_PRIORITY);

	RegisterInputBehavior(MultiButtonClickDragBehavior);
}

FInputRayHit UViewportPanInteraction::CanBeginClickDragSequence(const FInputDeviceRay& InPressPos)
{
	bool bSuccess = CanBeActivated();

	if (FEditorViewportClient* EditorViewportClient = GetEditorViewportClient())
	{
		bSuccess &= EditorViewportClient->IsPerspective();
	}

	return bSuccess ? FInputRayHit(TNumericLimits<float>::Max()) : FInputRayHit();
}

void UViewportPanInteraction::OnClickPress(const FInputDeviceRay& PressPos)
{
	UE::Editor::ViewportInteractions::LOG(TEXT("PAN 1"));

	UViewportDragInteraction::OnClickPress(PressPos);

	if (UViewportInteractionsBehaviorSource* BehaviorSource = GetViewportInteractionsBehaviorSource())
	{
		BehaviorSource->SetIsMouseLooking(true);
	}

	bMultiButtonPanInteraction = false;
}

void UViewportPanInteraction::OnClickRelease(const FInputDeviceRay& ReleasePos)
{
	UE::Editor::ViewportInteractions::LOG(TEXT("PAN 0"));

	UViewportDragInteraction::OnClickRelease(ReleasePos);

	if (UViewportInteractionsBehaviorSource* BehaviorSource = GetViewportInteractionsBehaviorSource())
	{
		BehaviorSource->SetIsMouseLooking(false);
	}
}

void UViewportPanInteraction::OnDragDelta(float InMouseDeltaX, float InMouseDeltaY)
{
	if (FEditorViewportClient* const EditorViewportClient = GetEditorViewportClient())
	{
		// Pan left/right/up/down
		// TODO: In legacy, this bool will also consider if using trackpad or not - possibly need to handle this in a different way with ITF?

		bool bInvert = GetDefault<ULevelEditorViewportSettings>()->bInvertMiddleMousePan;

		FVector LocationDelta;

		const FRotator& ViewRotation = EditorViewportClient->GetViewRotation();
		if (bMultiButtonPanInteraction)
		{
			float Direction = bInvert ? 1.0f : -1.0f;
			LocationDelta.X = -InMouseDeltaX * Direction * FMath::Sin( ViewRotation.Yaw * PI / 180.0f );
			LocationDelta.Y = -InMouseDeltaX * -Direction * FMath::Cos( ViewRotation.Yaw * PI / 180.0f );
			LocationDelta.Z = -Direction * InMouseDeltaY;
		}
		else
		{
			LocationDelta = FQuat(ViewRotation) * FVector( 0.0f, InMouseDeltaX, -InMouseDeltaY) * (bInvert ? 1.0f : -1.0f);
		}

		FVector& ViewportLocationDelta = EditorViewportClient->GetViewportNavigationHelper()->LocationDelta;
		ViewportLocationDelta += LocationDelta;
	}
}

FDeviceButtonState UViewportPanInteraction::GetActiveMouseButtonState(const FInputDeviceState& Input)
{
	return Input.Mouse.Middle;
}

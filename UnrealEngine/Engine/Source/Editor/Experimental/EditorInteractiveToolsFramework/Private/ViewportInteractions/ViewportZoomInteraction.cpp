// Copyright Epic Games, Inc. All Rights Reserved.

#include "ViewportInteractions/ViewportZoomInteraction.h"
#include "BaseBehaviors/ClickDragBehavior.h"
#include "BaseBehaviors/KeyInputBehavior.h"
#include "Behaviors/MultiButtonClickDragBehavior.h"
#include "EditorViewportClient.h"
#include "Settings/LevelEditorViewportSettings.h"
#include "Templates/Function.h"
#include "ViewportClientNavigationHelper.h"
#include "ViewportInteractions/ViewportInteractionsBehaviorSource.h"

UViewportZoomInteraction::UViewportZoomInteraction()
{
	ToolType = UE::Editor::ViewportInteractions::Zoom;

	InteractionName = TEXT("Zoom");

	// Mousewheel zoom
	UMouseWheelInputBehavior* MouseWheelInputBehavior = NewObject<UMouseWheelInputBehavior>();
	MouseWheelInputBehavior->Initialize(this);
	MouseWheelInputBehavior->SetDefaultPriority(UE::Editor::ViewportInteractions::VIEWPORT_INTERACTIONS_DEFAULT_PRIORITY);

	// Keyboard zoom
	UKeyInputBehavior* KeyInputBehavior = NewObject<UKeyInputBehavior>();
	// Hardcoded
	KeyInputBehavior->Initialize(this, { ZoomIn, ZoomOut });
	KeyInputBehavior->bRequireAllKeys = false;
	KeyInputBehavior->SetDefaultPriority(UE::Editor::ViewportInteractions::VIEWPORT_INTERACTIONS_DEFAULT_PRIORITY);

	// LMB + RMB + Drag zoom.
	UMultiButtonClickDragBehavior* MultiButtonClickDragBehavior = NewObject<UMultiButtonClickDragBehavior>();
	MultiButtonClickDragBehavior->Initialize();
	MultiButtonClickDragBehavior->EnableButton(EKeys::LeftMouseButton);
	MultiButtonClickDragBehavior->EnableButton(EKeys::RightMouseButton);

	MultiButtonClickDragBehavior->CanBeginClickDragFunc = [this](const FInputDeviceRay&)
	{
		bool bSuccess = IsEnabled();
		if (FEditorViewportClient* EditorViewportClient = GetEditorViewportClient())
		{
			bSuccess &= EditorViewportClient->IsOrtho();
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

	RegisterInputBehaviors({ MouseWheelInputBehavior, KeyInputBehavior, MultiButtonClickDragBehavior });
}

FInputRayHit UViewportZoomInteraction::ShouldRespondToMouseWheel(const FInputDeviceRay& InCurrentPos)
{
	return IsEnabled() && !IsMouseLooking() ? FInputRayHit(TNumericLimits<float>::Max()) : FInputRayHit();
}

void UViewportZoomInteraction::OnMouseWheelScrollUp(const FInputDeviceRay& InCurrentPos)
{
	if (!IsMouseLooking())
	{
		constexpr bool bZoomIn = false;
		MouseWheelZoom(bZoomIn);
	}
}

void UViewportZoomInteraction::OnMouseWheelScrollDown(const FInputDeviceRay& InCurrentPos)
{
	if (!IsMouseLooking())
	{
		constexpr bool bZoomIn = true;
		MouseWheelZoom(bZoomIn);
	}
}

void UViewportZoomInteraction::OnKeyPressed(const FKey& InKeyID)
{
	if (InKeyID == ZoomIn)
	{
		ZoomDirection = -1;
	}
	else if (InKeyID == ZoomOut)
	{
		ZoomDirection = 1;
	}
}

void UViewportZoomInteraction::OnKeyReleased(const FKey& InKeyID)
{
	if (InKeyID == EKeys::Add || InKeyID == EKeys::Subtract)
	{
		ZoomDirection = 0;
	}
}

void UViewportZoomInteraction::OnForceEndCapture()
{
	ZoomDirection = 0;
	OnClickRelease(FInputDeviceRay());
}

void UViewportZoomInteraction::Tick(float InDeltaTime) const
{
	if (FEditorViewportClient* EditorViewportClient = GetEditorViewportClient())
	{
		if (ZoomDirection != 0)
		{
			const bool bZoomIn = ZoomDirection > 0;
			if (EditorViewportClient->IsPerspective())
			{
				PerspectiveZoom(bZoomIn);
			}
			else
			{
				// Slower zoom speed, and always centered when done in orthographic view
				OrthographicZoom(bZoomIn, 0.25f, true);
			}
		}
	}

	UViewportInteraction::Tick(InDeltaTime);
}

void UViewportZoomInteraction::OrthographicZoom(bool bInZoomIn, float InZoomMultiplier, bool bInForceZoomToCenter) const
{
	FEditorViewportClient* EditorViewportClient = GetEditorViewportClient();
	if (!EditorViewportClient)
	{
		return;
	}

	if (const FViewport* const Viewport = EditorViewportClient->Viewport)
	{
		constexpr float Scale = 1.0f;

		// Scrolling the mousewheel up/down zooms the orthogonal viewport in/out.
		float NewOrthoRatio = 1.0f + ((25.0f * Scale) / CAMERA_ZOOM_DAMPEN) * InZoomMultiplier;
		if (!bInZoomIn)
		{
			NewOrthoRatio = 1.0f / NewOrthoRatio;
		}

		// Extract current state
		int32 ViewportWidth = Viewport->GetSizeXY().X;
		int32 ViewportHeight = Viewport->GetSizeXY().Y;

		FVector OldOffsetFromCenter;

		const bool bCenterZoomAroundCursor = GetDefault<ULevelEditorViewportSettings>()->bCenterZoomAroundCursor;

		if (bCenterZoomAroundCursor && !bInForceZoomToCenter)
		{
			// Y is actually backwards, but since we're move the camera opposite the cursor to center, we negate both
			// therefore the x is negated
			// X Is backwards, negate it
			// default to viewport mouse position
			int32 CenterX = Viewport->GetMouseX();
			int32 CenterY = Viewport->GetMouseY();

			//TODO: check this one out
			/*if (EditorViewportClient->ShouldUseMoveCanvasMovement())
			{
				// use virtual mouse while dragging (normal mouse is clamped when invisible)
				CenterX = EditorViewportClient->LastMouseX;
				CenterY = EditorViewportClient->LastMouseY;
			}*/
			int32 DeltaFromCenterX = -(CenterX - (ViewportWidth >> 1));
			int32 DeltaFromCenterY = (CenterY - (ViewportHeight >> 1));

			switch (EditorViewportClient->GetViewportType())
			{
			case LVT_OrthoTop:
				OldOffsetFromCenter.Set(-DeltaFromCenterY, -DeltaFromCenterX, 0.0f);
				break;
			case LVT_OrthoLeft:
				OldOffsetFromCenter.Set(DeltaFromCenterX, 0.0f, DeltaFromCenterY);
				break;
			case LVT_OrthoBack:
				OldOffsetFromCenter.Set(0.0f, DeltaFromCenterX, DeltaFromCenterY);
				break;
			case LVT_OrthoBottom:
				OldOffsetFromCenter.Set(-DeltaFromCenterY, DeltaFromCenterX, 0.0f);
				break;
			case LVT_OrthoRight:
				OldOffsetFromCenter.Set(-DeltaFromCenterX, 0.0f, DeltaFromCenterY);
				break;
			case LVT_OrthoFront:
				OldOffsetFromCenter.Set(0.0f, -DeltaFromCenterX, DeltaFromCenterY);
				break;
			case LVT_OrthoFreelook:
				//@TODO: CAMERA: How to handle this (todo copied from legacy viewport inputs)
				break;
			case LVT_Perspective:
				break;
			}
		}

		// Save off old zoom
		const float OldUnitsPerPixel = EditorViewportClient->GetOrthoUnitsPerPixel(Viewport);

		// Update zoom based on input
		const float Zoom = EditorViewportClient->GetOrthoZoom() * NewOrthoRatio;

		const float MinOrthoZoom = static_cast<float>(
			FMath::Max(GetDefault<ULevelEditorViewportSettings>()->MinimumOrthographicZoom, MIN_ORTHOZOOM)
		);

		EditorViewportClient->SetOrthoZoom(static_cast<float>(FMath::Clamp(Zoom, MinOrthoZoom, MAX_ORTHOZOOM)));

		if (bCenterZoomAroundCursor)
		{
			// This is the equivalent to moving the viewport to center about the cursor, zooming, and moving it back a proportional amount towards the cursor
			const FVector FinalDelta = (EditorViewportClient->GetOrthoUnitsPerPixel(Viewport) - OldUnitsPerPixel)
									 * OldOffsetFromCenter;

			// Now move the view location proportionally
			EditorViewportClient->SetViewLocation(EditorViewportClient->GetViewLocation() + FinalDelta);
		}

		constexpr bool bInvalidateViews = true;

		// Update linked ortho viewport movement based on updated zoom and view location,
		EditorViewportClient->UpdateLinkedOrthoViewports(bInvalidateViews);
		EditorViewportClient->Invalidate(true, true);

		UE::Editor::ViewportInteractions::LOG(TEXT("ITF CAMERA ORTHO ZOOM"));
	}
}

void UViewportZoomInteraction::PerspectiveZoom(bool bInZoomIn) const
{
	FEditorViewportClient* EditorViewportClient = GetEditorViewportClient();

	// Scrolling the mousewheel up/down moves the perspective viewport forwards/backwards.
	FVector Drag;

	const FRotator& ViewRotation = EditorViewportClient->GetViewRotation();
	Drag.X = FMath::Cos(ViewRotation.Yaw * PI / 180.0f) * FMath::Cos(ViewRotation.Pitch * PI / 180.0f);
	Drag.Y = FMath::Sin(ViewRotation.Yaw * PI / 180.0f) * FMath::Cos(ViewRotation.Pitch * PI / 180.0f);
	Drag.Z = FMath::Sin(ViewRotation.Pitch * PI / 180.0f);

	if (bInZoomIn)
	{
		Drag = -Drag;
	}

	const float CameraSpeed = EditorViewportClient->GetCameraSpeedSettings().GetCurrentSpeed();
	Drag *= CameraSpeed * 32.0f;

	constexpr bool bDollyCamera = true;
	EditorViewportClient->MoveViewportCamera(Drag, FRotator::ZeroRotator, bDollyCamera);

	FEditorDelegates::OnDollyPerspectiveCamera.Broadcast(Drag, EditorViewportClient->ViewIndex);

	EditorViewportClient->Invalidate(true, true);

	UE::Editor::ViewportInteractions::LOG(TEXT("ITF CAMERA PERSPECTIVE ZOOM"));
}

void UViewportZoomInteraction::MouseWheelZoom(bool bInZoomIn) const
{
	FEditorViewportClient* EditorViewportClient = GetEditorViewportClient();

	if (EditorViewportClient->IsPerspective())
	{
		PerspectiveZoom(bInZoomIn);
	}
	else
	{
		OrthographicZoom(bInZoomIn);
	}
}

void UViewportZoomInteraction::OnClickPress(const FInputDeviceRay& InPressPos)
{
	UE::Editor::ViewportInteractions::LOG(TEXT("ORTHO ZOOM LMB + RMB 1"));

	if (UViewportInteractionsBehaviorSource* BehaviorSource = GetViewportInteractionsBehaviorSource())
	{
		BehaviorSource->SetMouseCursorOverride(EMouseCursor::None);
		BehaviorSource->SetIsMouseLooking(true);
	}

	LastZoomDragY = static_cast<float>(InPressPos.ScreenPosition.Y);
}

void UViewportZoomInteraction::OnClickRelease(const FInputDeviceRay& InReleasePos)
{
	UE::Editor::ViewportInteractions::LOG(TEXT("ORTHO ZOOM LMB + RMB 0"));

	if (UViewportInteractionsBehaviorSource* BehaviorSource = GetViewportInteractionsBehaviorSource())
	{
		BehaviorSource->SetIsMouseLooking(false);
	}
}

void UViewportZoomInteraction::OnClickDrag(const FInputDeviceRay& InDragPos)
{
	const float ScreenPosY = static_cast<float>(InDragPos.ScreenPosition.Y);
	const float DeltaY = ScreenPosY - LastZoomDragY;

	if (FEditorViewportClient* const EditorViewportClient = GetEditorViewportClient())
	{
		FVector LocationDelta(0.0f, 0.0f, -DeltaY);
		FVector& ViewportLocationDelta = EditorViewportClient->GetViewportNavigationHelper()->LocationDelta;
		ViewportLocationDelta += LocationDelta;
	}

	LastZoomDragY = ScreenPosY;
}

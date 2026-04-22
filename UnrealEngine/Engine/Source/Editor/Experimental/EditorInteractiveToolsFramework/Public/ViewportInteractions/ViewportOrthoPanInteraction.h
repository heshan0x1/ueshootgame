// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ViewportDragInteraction.h"

#include "ViewportOrthoPanInteraction.generated.h"

#define UE_API EDITORINTERACTIVETOOLSFRAMEWORK_API

/**
 * Implements panning for orthographic views. Holding Alt or Shift inverts the panning direction.
 */
UCLASS(MinimalAPI)
class UViewportOrthoPanInteraction : public UViewportDragInteraction
{
	GENERATED_BODY()

public:
	UE_API UViewportOrthoPanInteraction();

	//~ Begin IClickDragBehaviorTarget
	UE_API virtual FInputRayHit CanBeginClickDragSequence(const FInputDeviceRay& InPressPos) override;
	UE_API virtual void OnClickPress(const FInputDeviceRay& PressPos) override;
	UE_API virtual void OnClickRelease(const FInputDeviceRay& ReleasePos) override;
	//~ End IClickDragBehaviorTarget

	//~ Begin UViewportDragInteraction
	UE_API virtual void OnDragDelta(float InMouseDeltaX, float InMouseDeltaY) override;
	UE_API virtual FDeviceButtonState GetActiveMouseButtonState(const FInputDeviceState& Input) override;
	//~ End UViewportDragInteraction

	UE_API virtual bool CanBeActivated(const FInputDeviceState& InInputDeviceState = FInputDeviceState()) const override;
};

#undef UE_API

// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ViewportDragInteraction.h"

#include "ViewportPanInteraction.generated.h"

#define UE_API EDITORINTERACTIVETOOLSFRAMEWORK_API

class UViewportInteractionsBehaviorSource;

/**
 * Implements the LMB + Drag Horizontal plane move + Yaw interaction
 */
UCLASS(MinimalAPI, Transient)
class UViewportPanInteraction : public UViewportDragInteraction
{
	GENERATED_BODY()

public:
	UE_API UViewportPanInteraction();

	//~ Begin IClickDragBehaviorTarget
	UE_API virtual FInputRayHit CanBeginClickDragSequence(const FInputDeviceRay& InPressPos) override;
	UE_API virtual void OnClickPress(const FInputDeviceRay& PressPos) override;
	UE_API virtual void OnClickRelease(const FInputDeviceRay& ReleasePos) override;
	//~ End IClickDragBehaviorTarget

	//~ Begin UViewportDragInteraction
	UE_API virtual void OnDragDelta(float InMouseDeltaX, float InMouseDeltaY) override;
	UE_API virtual FDeviceButtonState GetActiveMouseButtonState(const FInputDeviceState& Input) override;
	//~ End UViewportDragInteraction

private:
	// Pan direction can be inverted e.g. with a key modifier
	bool bMultiButtonPanInteraction = false;
};

#undef UE_API

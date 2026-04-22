// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ViewportDragInteraction.h"

#include "ViewportMoveYawInteraction.generated.h"

#define UE_API EDITORINTERACTIVETOOLSFRAMEWORK_API

class UViewportInteractionsBehaviorSource;

/**
 * Implements the LMB + Drag Horizontal plane move + Yaw interaction
 */
UCLASS(MinimalAPI, Transient)
class UViewportMoveYawInteraction
	: public UViewportDragInteraction
{
	GENERATED_BODY()

public:
	UE_API UViewportMoveYawInteraction();

	//~ Begin IClickDragBehaviorTarget
	UE_API virtual FInputRayHit CanBeginClickDragSequence(const FInputDeviceRay& InPressPos) override;
	UE_API virtual void OnClickPress(const FInputDeviceRay& PressPos) override;
	UE_API virtual void OnClickRelease(const FInputDeviceRay& ReleasePos) override;
	//~ End IClickDragBehaviorTarget

	//~ Begin UViewportDragInteraction
	UE_API virtual void OnDragDelta(float InMouseDeltaX, float InMouseDeltaY) override;
	UE_API virtual FDeviceButtonState GetActiveMouseButtonState(const FInputDeviceState& Input) override;
	UE_API virtual bool CanBeActivated(const FInputDeviceState& InInputDeviceState = FInputDeviceState()) const override;
	//~ End UViewportDragInteraction
};

#undef UE_API

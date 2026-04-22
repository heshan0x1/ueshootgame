// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ViewportDragInteraction.h"

#include "ViewportOrbitInteraction.generated.h"

#define UE_API EDITORINTERACTIVETOOLSFRAMEWORK_API

/**
 * Triggers orbiting around current viewport focus
 */
UCLASS(MinimalAPI, Transient)
class UViewportOrbitInteraction
	: public UViewportDragInteraction
{
	GENERATED_BODY()

public:
	UE_API UViewportOrbitInteraction();

	//~ Begin IClickDragBehaviorTarget
	UE_API virtual FInputRayHit CanBeginClickDragSequence(const FInputDeviceRay& InPressPos) override;
	UE_API virtual void OnClickPress(const FInputDeviceRay& InPressPos) override;
	UE_API virtual void OnClickRelease(const FInputDeviceRay& InReleasePos) override;
	//~ End IClickDragBehaviorTarget

	//~ Begin UViewportDragInteraction
	UE_API virtual void OnDragDelta(float InMouseDeltaX, float InMouseDeltaY) override;
	UE_API virtual bool CanBeActivated(const FInputDeviceState& InInputDeviceState = FInputDeviceState()) const override;
	//~ End UViewportDragInteraction
};

#undef UE_API

// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MultiButtonClickDragBehavior.h"
#include "ViewportClickDragBehavior.generated.h"

/**
 * 
 */
UCLASS(MinimalAPI)
class UViewportClickDragBehavior : public UMultiButtonClickDragBehavior
{
	GENERATED_BODY()

public:

	/**
	 * Set the priority this Drag Behavior will return from GetPriority once the current interaction is confirmed to be a drag.
	 * Should be higher than its DefaultPriority, and higher than the priority of any other behavior it wants to steal capture from
	 */
	EDITORINTERACTIVETOOLSFRAMEWORK_API void SetDragConfirmedPriority(FInputCapturePriority InDragConfirmedPriority);

	EDITORINTERACTIVETOOLSFRAMEWORK_API virtual FInputCapturePriority GetPriority() override;

	virtual FInputCaptureRequest WantsCapture(const FInputDeviceState& InputState) override;

protected:

	/** @return true if any captured button was pressed. */
	bool IsAnyButtonPressed(const FInputDeviceState& InInput);

	/** Distance after which a drag is considered such */
	float ClickDistanceThreshold = 5.0f;

	/** Cached cursor position */
	FVector2D MouseDownPosition;

	float MouseTraveledDistance = 0.0f;

	/**
	 * The priority this Drag Behavior will return from GetPriority once the current interaction is confirmed to be a drag.
	 * Should be higher than its DefaultPriority, and higher than the priority of any other behavior it wants to steal capture from
	 */
	FInputCapturePriority DragConfirmedPriority;
};

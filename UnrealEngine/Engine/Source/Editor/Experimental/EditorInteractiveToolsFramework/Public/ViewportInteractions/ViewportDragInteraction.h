// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "BaseBehaviors/BehaviorTargetInterfaces.h"
#include "ViewportInteraction.h"

#include "ViewportDragInteraction.generated.h"

#define UE_API EDITORINTERACTIVETOOLSFRAMEWORK_API

class UClickDragInputBehavior;
class USingleClickOrDragInputBehavior;

/**
 * A base class which can be used to implement interactions based on mouse drag actions
 */
UCLASS(MinimalAPI, Transient, Abstract)
class UViewportDragInteraction
	: public UViewportInteraction
	, public IClickDragBehaviorTarget
{
	GENERATED_BODY()
public:
	UE_API UViewportDragInteraction();

	//~ Begin IClickDragBehaviorTarget
	UE_API virtual FInputRayHit CanBeginClickDragSequence(const FInputDeviceRay& InPressPos) override;
	UE_API virtual void OnClickPress(const FInputDeviceRay& InPressPos) override;
	UE_API virtual void OnClickDrag(const FInputDeviceRay& InDragPos) override;
	UE_API virtual void OnClickRelease(const FInputDeviceRay& InReleasePos) override;
	virtual void OnTerminateDragSequence() override {};
	//~ End IClickDragBehaviorTarget

	//~ Begin IModifierToggleBehaviorTarget
	UE_API virtual void OnForceEndCapture() override;
	//~ End IModifierToggleBehaviorTarget

	//~ Begin FViewportInputToolBase
	UE_API virtual void Initialize(UViewportInteractionsBehaviorSource* const InViewportInteractionsBehaviorSource) override;
	//~ End FViewportInputToolBase

	virtual void OnDragDelta(float InMouseDeltaX, float InMouseDeltaY)
	{
	}

	virtual bool CanBeActivated(const FInputDeviceState& InInputDeviceState = FInputDeviceState()) const
	{
		return IsEnabled();
	}

	/**
	 * Provides a click behavior target to be called when drag interaction fails
	 * @param InClickBehaviorTarget the Click behavior target responding to clicks
	 */
	UE_API void SetClickTarget(IClickBehaviorTarget* InClickBehaviorTarget);

protected:

	UE_API void SetClickOrDragPriority(int InPriority) const;
	
	/**
	 * Returns the state of the mouse button currently used by this Interaction. Left mouse by default, override for other buttons.
	 */
	UE_API virtual FDeviceButtonState GetActiveMouseButtonState(const FInputDeviceState& Input);

	TWeakObjectPtr<USingleClickOrDragInputBehavior> ClickOrDragInputBehavior;

	bool bIsDragging;
	int32 MouseX;
	int32 MouseY;
};

#undef UE_API

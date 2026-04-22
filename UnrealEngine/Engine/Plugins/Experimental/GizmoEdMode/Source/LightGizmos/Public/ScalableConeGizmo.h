// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "InteractiveGizmo.h"
#include "InteractiveGizmoBuilder.h"
#include "BaseBehaviors/BehaviorTargetInterfaces.h"
#include "BaseBehaviors/AnyButtonInputBehavior.h"

#include "UObject/Package.h"
#include "ScalableConeGizmo.generated.h"

#define UE_API LIGHTGIZMOS_API

class UTransformProxy;
struct FHitResult;

UCLASS(MinimalAPI)
class UScalableConeGizmoBuilder : public UInteractiveGizmoBuilder
{
	GENERATED_BODY()

public:
	UE_API virtual UInteractiveGizmo* BuildGizmo(const FToolBuilderState& SceneState) const override;
};

/**
 * UScalableConeGizmo provides a cone that can be scaled (changing its angle)
 * by dragging the base of the cone outwards/inwards
 */
UCLASS(MinimalAPI)
class UScalableConeGizmo : public UInteractiveGizmo, public IHoverBehaviorTarget
{
	GENERATED_BODY()

public:
	// UInteractiveGizmo interface

	UE_API virtual void Setup() override;

	UE_API virtual void Render(IToolsContextRenderAPI* RenderAPI) override;

	// Set the target to attach the gizmo to
	UE_API virtual void SetTarget(UTransformProxy* InTarget);

	// Gettors and Settors for the Angle and Length
	UE_API void SetAngleDegrees(float InAngle);
	UE_API void SetLength(float InLength);
	UE_API float GetLength();
	UE_API float GetAngleDegrees();

	// IHoverBehaviorTarget interface
	UE_API virtual FInputRayHit BeginHoverSequenceHitTest(const FInputDeviceRay& PressPos) override;
	virtual void OnBeginHover(const FInputDeviceRay& DevicePos) override {}
	UE_API virtual bool OnUpdateHover(const FInputDeviceRay& DevicePos) override;
	UE_API virtual void OnEndHover() override;

	UE_API virtual void OnBeginDrag(const FInputDeviceRay& Ray);
	UE_API virtual void OnUpdateDrag(const FInputDeviceRay& Ray);
	UE_API virtual void OnEndDrag(const FInputDeviceRay& Ray);

	/** Check if the input ray hits the cone. The hit testable parts of the cone currently is just 
	 *  the circle at its base
	 */
	UE_API bool HitTest(const FRay& Ray, FHitResult& OutHit, FVector& OutAxis, FTransform& OutTransform);

	// The maximum angle the cone can be stretched to
	UPROPERTY()
	float MaxAngle;

	// The minimum angle the cone can be stretched to
	UPROPERTY()
	float MinAngle;

	// The color of the cone
	UPROPERTY()
	FColor ConeColor;

	// The error threshold for hit detection with the cone
	UPROPERTY()
	float HitErrorThreshold{ 12.f };

	// The text that will be used as the transaction description for undo/redo
	UPROPERTY()
	FText TransactionDescription;

	/** Called when the Angle of the cone is changed. Sends new angle as parameter. */
	TFunction<void(const float)> UpdateAngleFunc = nullptr;

private:

	// The ConeLength
	UPROPERTY()
	float Length;

	UPROPERTY()
	float Angle;

	/** Whether the gizmo is being hovered over */
	UPROPERTY()
	bool bIsHovering{ false };

	/** Whether the gizmo is being dragged */
	UPROPERTY()
	bool bIsDragging{ false };

	UPROPERTY()
	TObjectPtr<UTransformProxy> ActiveTarget;

	/** Used for calculations when moving the handles*/

	UPROPERTY()
	FVector DragStartWorldPosition;

	// The position the drag is on currently (projected onto the line it is being dragged along)
	UPROPERTY()
	FVector DragCurrentPositionProjected;

	UPROPERTY()
	FVector InteractionStartPoint;

	UPROPERTY()
	float InteractionStartParameter;

	UPROPERTY()
	FVector HitAxis;

	UPROPERTY()
	FVector RotationPlaneX;

	UPROPERTY()
	FVector RotationPlaneY;
};

/**
 * A behavior that forwards clicking and dragging to the gizmo.
 */
UCLASS(MinimalAPI)
class UScalableConeGizmoInputBehavior : public UAnyButtonInputBehavior
{
	GENERATED_BODY()

public:
	virtual FInputCapturePriority GetPriority() override { return FInputCapturePriority(FInputCapturePriority::DEFAULT_GIZMO_PRIORITY); }

	UE_API virtual void Initialize(UScalableConeGizmo* Gizmo);

	UE_API virtual FInputCaptureRequest WantsCapture(const FInputDeviceState& input) override;
	UE_API virtual FInputCaptureUpdate BeginCapture(const FInputDeviceState& input, EInputCaptureSide eSide) override;
	UE_API virtual FInputCaptureUpdate UpdateCapture(const FInputDeviceState& input, const FInputCaptureData& data) override;
	UE_API virtual void ForceEndCapture(const FInputCaptureData& data) override;

protected:
	UScalableConeGizmo* Gizmo;
	FRay LastWorldRay;
	FVector2D LastScreenPosition;
	bool bInputDragCaptured;

};

#undef UE_API

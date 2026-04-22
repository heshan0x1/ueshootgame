// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "BaseGizmos/TransformProxy.h"
#include "EditorGizmos/TransformGizmo.h"
#include "UObject/GCObject.h"

#define UE_API EDITORINTERACTIVETOOLSFRAMEWORK_API

class FLevelEditorViewportClient;
class UEditorTransformGizmo;
class UInteractiveToolManager;
class UTransformProxy;

/**
 * Listens for key press of a specified modifier key, and if Level Viewport Gizmo is dragging, it duplicates current selection.
 * Currently used with Shift (see UDragToolsBehaviorSource)
 */
class FEditorMoveCameraWithObject
	: public IModifierToggleBehaviorTarget
	, public FGCObject
{
public:
	UE_API FEditorMoveCameraWithObject(UInteractiveToolManager* InToolManager);

	UE_API virtual ~FEditorMoveCameraWithObject() override;

protected:
	//~Begin IModifierToggleBehaviorTarget
	UE_API virtual void OnUpdateModifierState(int InModifierID, bool bInIsOn) override;
	//~End IModifierToggleBehaviorTarget

	//~Begin FGCObject
	UE_API virtual void AddReferencedObjects(FReferenceCollector& InCollector) override;
	virtual FString GetReferencerName() const override
	{
		return TEXT("FEditorMoveCameraWithObject");
	}
	//~End FGCObject

	UE_API void OnGizmoMovementBegin(UTransformProxy* InTransformProxy);
	UE_API void OnGizmoMovementEnd(UTransformProxy* InTransformProxy);
	UE_API void OnGizmoTransformChanged(UTransformProxy* InTransformProxy, UE::Math::TTransform<double> InTransform);

	UE_API void Initialize();
	UE_API void Reset();

	UE_API void OnGizmoCreatedDelegate(UTransformGizmo* InTransformGizmo);
	UE_API void OnUsesNewTRSGizmosChanged(bool bInUseNewTRSGizmos);

private:
	TObjectPtr<UInteractiveToolManager> ToolManager;
	TObjectPtr<UEditorTransformGizmo> TransformGizmo;

	bool bGizmoIsDragged = false;
	bool bModifierKeyIsPressed = false;

	FDelegateHandle OnBeginPivotEditDelegate;
	FDelegateHandle OnEndPivotEditDelegate;
	FDelegateHandle OnTransformChangedDelegate;
};

#undef UE_API

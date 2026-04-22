// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EditorViewportClient.h"
#include "InputBehaviorSet.h"
#include "UObject/Object.h"
#include "ViewportInteractions/ViewportInteraction.h"
#include "DragToolsBehaviourSource.generated.h"


namespace UE::Editor::DragTools
{

// In case New TRS Gizmos are enabled, we need Mouse Drag + Key modifier Drag Tools Behaviors to be processed before
// the Gizmo ones, and also before viewport interactions - otherwise Drag Behaviors will never be triggered. See:
// * UTransformGizmo::SetupBehaviors()			[DEFAULT_GIZMO_PRIORITY]
// * UTransformGizmo::SetupIndirectBehaviors()	[DEFAULT_GIZMO_PRIORITY - 1]

static constexpr int HANDLED_BEFORE_VIEWPORT_INTERACTIONS =
	UE::Editor::ViewportInteractions::VIEWPORT_INTERACTIONS_HIGH_PRIORITY - 1;

// Conversely, tools which are activated with just mouse buttons input (no key modifiers, mouse drag behavior only) need to be processed after Gizmo Behaviors
static constexpr int HANDLED_AFTER_GIZMO_PRIORITY = FInputCapturePriority::DEFAULT_GIZMO_PRIORITY + 1;

static constexpr int DRAG_TOOLS_DEFAULT_PRIORITY = HANDLED_BEFORE_VIEWPORT_INTERACTIONS - 10;
static constexpr int DRAG_TOOLS_LOW_PRIORITY = HANDLED_BEFORE_VIEWPORT_INTERACTIONS - 1;

static constexpr int DUPLICATE_SELECTION_PRIORITY = DRAG_TOOLS_DEFAULT_PRIORITY;
static constexpr int FRUSTUM_SELECT_PRIORITY = DRAG_TOOLS_DEFAULT_PRIORITY;
static constexpr int VIEWPORT_CHANGE_PRIORITY = DRAG_TOOLS_DEFAULT_PRIORITY;
static constexpr int BOX_SELECT_PRIORITY = HANDLED_AFTER_GIZMO_PRIORITY;
static constexpr int MEASURE_PRIORITY = HANDLED_AFTER_GIZMO_PRIORITY;

}

class FEditorDuplicateDragSelection;
class FEditorDragToolBehaviorTarget;
class FEditorMoveCameraWithObject;
class UEditorInteractiveToolsContext;

/**
 * Hosts Drag Tools and needed behaviors.
 * Handles an Input Behavior Set, and keeps track of currently active Drag tool.
 */
UCLASS(Transient, MinimalAPI)
class UDragToolsBehaviorSource final : public UObject, public IInputBehaviorSource
{
	GENERATED_BODY()

public:
	static bool IsViewportChangeToolEnabled();

	DECLARE_MULTICAST_DELEGATE(FOnOnViewportChangeToolToggleDelegate)
	static FOnOnViewportChangeToolToggleDelegate& OnViewportChangeToolActivated();
	static FOnOnViewportChangeToolToggleDelegate& OnViewportChangeToolDeactivated();

	//~ Begin IInputBehaviorSource
	virtual const UInputBehaviorSet* GetInputBehaviors() const override { return BehaviorSet; }
	//~ End IClickDragBehaviorTarget

	/**
	 * Register this Input Behavior Source to the InputRouter
	 */
	EDITORINTERACTIVETOOLSFRAMEWORK_API void RegisterSource();

	/**
	 * Deregister this Input Behavior Source from the InputRouter
	 */
	EDITORINTERACTIVETOOLSFRAMEWORK_API void DeregisterSource();

	/**
	 * Creates and stores the drag tools.
	 * Instantiates the BehaviorSet hosting all behaviors required by drag tools.
	 *
	 * @param InInteractiveToolsContext: used to retrieve the viewport client used by drag tools. Can be gathered e.g.
	 * from UEdMode::GetInteractiveToolsContext.
	 */
	EDITORINTERACTIVETOOLSFRAMEWORK_API void Initialize(const UEditorInteractiveToolsContext* InInteractiveToolsContext);

	/**
	 * Renders the active tool on the specified View/Canvas
	 */
	EDITORINTERACTIVETOOLSFRAMEWORK_API void RenderTools(FViewportClient* InViewportClient, const FSceneView* InSceneView, FCanvas* InCanvas) const;

private:
	void OnActivateTool(EDragTool::Type InDragToolType);
	void OnDeactivateTool(EDragTool::Type InDragToolType);

	void ActivateViewportChangeTool();
	void DeactivateViewportChangeTool();

	/**
	 * Returns the drag tool currently being used, if any
	 */
	FEditorDragToolBehaviorTarget* GetActiveTool() const;

	FEditorViewportClient* GetFocusedEditorViewportClient() const;

	FEditorViewportClient* GetHoveredEditorViewportClient() const;

	/**
	 * Hosting drag tools behaviors
	 */
	UPROPERTY(Transient)
	TObjectPtr<UInputBehaviorSet> BehaviorSet;

	TWeakObjectPtr<const UEditorInteractiveToolsContext> EditorInteractiveToolsContextWeak;

	/**
	 * Available Drag Tools, access by using their type (EDragTool::Type)
	 */
	TMap<EDragTool::Type, TSharedPtr<FEditorDragToolBehaviorTarget>> DragTools;

	/**
	 * Duplicate dragged selection tool
	 */
	TSharedPtr<FEditorDuplicateDragSelection> DuplicateDragSelection;

	/**
	 * Move camera together with dragged selection
	 */
	TSharedPtr<FEditorMoveCameraWithObject> MoveCameraWithObject;

	/**
	 * Which type of Drag tool is currently active
	 */
	int ActiveToolType = -1;
};

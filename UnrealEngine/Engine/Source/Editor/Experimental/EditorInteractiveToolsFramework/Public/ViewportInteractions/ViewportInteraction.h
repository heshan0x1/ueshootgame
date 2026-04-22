// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "InputBehavior.h"
#include "Tools/EdModeInteractiveToolsContext.h"

#include "ViewportInteraction.generated.h"

#define UE_API EDITORINTERACTIVETOOLSFRAMEWORK_API

class FEditorViewportClient;
class FUICommandInfo;
class FViewportCameraMover;
class FViewportClickHandler;
class FViewportCommandsHandler;
class UInputBehavior;
class UViewportInteractionsBehaviorSource;

namespace UE::Editor::ViewportInteractions
{
constexpr int ShiftKeyMod = 1;
constexpr int AltKeyMod = 2;
constexpr int CtrlKeyMod = 3;

constexpr int LeftMouseButtonMod = 4;
constexpr int RightMouseButtonMod = 5;
constexpr int MiddleMouseButtonMod = 6;

static constexpr int VIEWPORT_INTERACTIONS_DEFAULT_PRIORITY = FInputCapturePriority::DEFAULT_GIZMO_PRIORITY - 4;
static constexpr int VIEWPORT_INTERACTIONS_LOW_PRIORITY = FInputCapturePriority::DEFAULT_GIZMO_PRIORITY - 3;
static constexpr int VIEWPORT_INTERACTIONS_HIGH_PRIORITY = FInputCapturePriority::DEFAULT_GIZMO_PRIORITY - 5;
static constexpr int VIEWPORT_INTERACTIONS_AFTER_GIZMOS_PRIORITY = FInputCapturePriority::DEFAULT_GIZMO_PRIORITY + 1;

/**
 * Possible types of Viewport Interactions.
 * Use by FEditorViewportClient to check whether a specific interaction is handled by an ITF-based interaction, or if it
 * should be handled by legacy logic
 */
static FName Zoom = TEXT("Zoom");
static FName Orbit = TEXT("Orbit");
static FName CameraDrag= TEXT("CameraDrag");
static FName CameraTranslate = TEXT("CameraTranslate");
static FName CameraRotate = TEXT("CameraRotate");
static FName FOV = TEXT("FOV");
static FName Commands = TEXT("Commands");
static FName SingleClick = TEXT("SingleClick");

} // namespace UE::Editor::ViewportInteractions

/**
 * Base class used to implement viewport interactions.
 * Has some helper functions to check if Shift, Alt, Ctrl modifier keys and Mouse Button keys are down.
 * Tick can be used for code which needs to affect things like camera movements in Editor Viewport Client
 */
UCLASS(MinimalAPI, Transient, Abstract)
class UViewportInteraction : public UObject
{
	GENERATED_BODY()

public:
	UE_API UViewportInteraction();

	UE_API bool IsShiftDown() const;
	UE_API bool IsAltDown() const;
	UE_API bool IsCtrlDown() const;

	UE_API bool IsLeftMouseButtonDown() const;
	UE_API bool IsMiddleMouseButtonDown() const;
	UE_API bool IsRightMouseButtonDown() const;

	UE_API bool IsAnyMouseButtonDown() const;

	UE_API bool IsMouseLooking() const;

	UE_API void SetEnabled(bool bInEnabled);
	UE_API bool IsEnabled() const;

	virtual void Tick(float InDeltaTime) const
	{
	}

	TArray<TObjectPtr<UInputBehavior>> GetInputBehaviors()
	{
		return InputBehaviors;
	}

	UE_API FName GetInteractionName() const;

	/**
	 * Initialization code which can be expanded by derived classes
	 */
	UE_API virtual void Initialize(UViewportInteractionsBehaviorSource* const InViewportInteractionsBehaviorSource);

	UE_API UViewportInteractionsBehaviorSource* GetViewportInteractionsBehaviorSource() const;

	//~ Begin UObject
	UE_API virtual void BeginDestroy() override;
	//~ Edn UObject

protected:
	UE_API FEditorViewportClient* GetEditorViewportClient() const;

	/**
	 * Define what should happen to UInputBehavior(s) when an "observed" command chord changes
	 */
	virtual void OnCommandChordChanged()
	{
	}

	/**
	 * Return a list of commands used by a key-based UInputBehavior.
	 * Used for its initialization, or its refresh, whenever a command chord changes
	 */
	virtual TArray<TSharedPtr<FUICommandInfo>> GetCommands() const
	{
		return {};
	}

	UE_API void RegisterInputBehavior(UInputBehavior* InBehavior);
	UE_API void RegisterInputBehaviors(TArray<UInputBehavior*> InBehaviors);

	UPROPERTY()
	TWeakObjectPtr<UViewportInteractionsBehaviorSource> ViewportInteractionsBehaviorSource;

	bool bEnabled = true;

	FName ToolType;

	/**
	 * Name for this interaction, used by Viewport Interactions Behavior Source to identify it.
	 * If none is set, static class name will be used.
	 */
	TOptional<FName> InteractionName;

private:
	UE_API void OnUserDefinedChordChanged(const FUICommandInfo& InCommandInfo);

	UPROPERTY()
	TArray<TObjectPtr<UInputBehavior>> InputBehaviors;

	FDelegateHandle OnChordChangedDelegateHandle;
};

#undef UE_API

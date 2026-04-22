// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "BaseBehaviors/BehaviorTargetInterfaces.h"
#include "InputBehaviorSet.h"
#include "Tools/EdModeInteractiveToolsContext.h"
#include "ViewportInteraction.h"

#include "ViewportInteractionsBehaviorSource.generated.h"

class FCameraControllerUserImpulseData;
class FEditorCameraController;
class FEditorViewportClient;
class FUICommandInfo;
class FViewportCameraMover;
class FViewportClickHandler;
class UEditorInteractiveToolsContext;
class UInputBehaviorSet;
class UViewportInteraction;

namespace UE::Editor::ViewportInteractions
{

DECLARE_MULTICAST_DELEGATE(FOnEditorViewportInteractionsToggleDelegate)

/**
 * Returns true if ITF-based input tools should be used.
 */
EDITORINTERACTIVETOOLSFRAMEWORK_API bool UseEditorViewportInteractions();

/**
 * Helps hiding unwanted logs. For testing.
 */
EDITORINTERACTIVETOOLSFRAMEWORK_API bool IsVerbose();

/**
 * Toggles ITF Viewport Interactions On/Off
 */
EDITORINTERACTIVETOOLSFRAMEWORK_API void ToggleEditorViewportInteractions(bool bInEnable);

EDITORINTERACTIVETOOLSFRAMEWORK_API FOnEditorViewportInteractionsToggleDelegate& OnEditorViewportInteractionsActivated();
EDITORINTERACTIVETOOLSFRAMEWORK_API FOnEditorViewportInteractionsToggleDelegate& OnEditorViewportInteractionsDeactivated();

/**
 * Checks whether the specified Command should be triggered by the specified Key
 */
bool CommandMatchesKey(const TSharedPtr<FUICommandInfo>& InCommandInfo, const FKey& InKeyID);

EDITORINTERACTIVETOOLSFRAMEWORK_API void LOG(const TCHAR* InMessage);

} // namespace UE::Editor::ViewportInteractions

DECLARE_MULTICAST_DELEGATE_OneParam(FOnMouseLookingChanged, bool /* bIsMouseLooking*/)

	/**
	 * This class hosts a list of ITF Viewport Interactions and a UBehaviorSet containing the UInteractionBehavior
	 * instances required by those Viewport Interactions.
	 * It also acts as a behavior target, registering the status of Shift, Alt, Ctrl modifier keys. That and other
	 * pieces of information can be accessed externally, so that e.g. Viewport Interactions can know about modifier
	 * states without having to implement themselves what would be duplicate logic to handle those inputs. This class
	 * can also be used to know whether the Viewport camera is being moved using the mouse,or to mark it as such.
	 */
	UCLASS(Transient, MinimalAPI)
class UViewportInteractionsBehaviorSource final
	: public UObject
	, public IInputBehaviorSource
	, public IModifierToggleBehaviorTarget
{
	GENERATED_BODY()

public:
	//~ Begin IInputBehaviorSource
	virtual const UInputBehaviorSet* GetInputBehaviors() const override
	{
		return BehaviorSet;
	}
	//~ End IClickDragBehaviorTarget

	//~ Begin IModifierToggleBehaviorTarget
	virtual void OnUpdateModifierState(int InModifierID, bool bInIsOn) override;
	virtual void OnForceEndCapture() override;
	//~ End IModifierToggleBehaviorTarget

	/**
	 * Will initialize the required behaviors and the Behavior Set
	 */
	EDITORINTERACTIVETOOLSFRAMEWORK_API void Initialize(const UEditorInteractiveToolsContext* InInteractiveToolsContext);

	/**
	 * Register this Input Behavior Source to the InputRouter
	 */
	EDITORINTERACTIVETOOLSFRAMEWORK_API void RegisterBehaviorSources();

	/**
	 * Deregister this Input Behavior Source from the InputRouter
	 */
	EDITORINTERACTIVETOOLSFRAMEWORK_API void DeregisterBehaviorSources();

	/**
	 * Call Tick on those Viewport Interactions which require it (e.g. Camera Mover)
	 */
	EDITORINTERACTIVETOOLSFRAMEWORK_API void Tick(float InDeltaTime) const;

	/**
	 * Add the specified interactions to this Behavior Source
	 */
	EDITORINTERACTIVETOOLSFRAMEWORK_API void AddInteractions(
		const TArray<const UClass*>& InInteractions, bool bInReregister = false
	);

	/**
	 *
	 * Add the specified interaction to this Behavior Source
	 */
	EDITORINTERACTIVETOOLSFRAMEWORK_API UViewportInteraction* AddInteraction(
		const UClass* InInteractionClass, bool bInReregister = false
	);

	/**
	 * De-registers behaviors, removes viewport interactions.
	 * Behaviors added by Initialize will be kept around.
	 */
	EDITORINTERACTIVETOOLSFRAMEWORK_API void Reset();

	/**
	 * Returns the first Viewport Interaction of type T
	 */
	template<class T>
	T* GetTypedViewportInteraction() const
	{
		T* Ret = nullptr;
		for (const TPair<FName, TObjectPtr<UViewportInteraction>>& Pair : ViewportInteractions)
		{
			if (UViewportInteraction* Interaction = Pair.Value)
			{
				if (Interaction->IsA<T>())
				{
					Ret = Cast<T>(Interaction);
				}
			}
		}

		return Ret;
	}

	/**
	 * Returns all Viewport Interactions of type T
	 */
	template<class T>
	TArray<T*> GetTypedViewportInteractions() const
	{
		TArray<T*> Return;

		for (const TPair<FName, TObjectPtr<UViewportInteraction>>& Pair : ViewportInteractions)
		{
			if (UViewportInteraction* Interaction = Pair.Value)
			{
				if (Interaction->IsA<T>())
				{
					Return.AddUnique(Cast<T>(Interaction));
				}
			}
		}

		return Return;
	}

	/**
	 * Sets current mouse cursor override
	 */
	void SetMouseCursorOverride(EMouseCursor::Type InMouseCursor);

	/**
	 * Use the templated version in case there is some class specific initialization required by the newly created interaction,
	 * since this method will return the newly instantiated interaction
	 */
	template<class T>
	T* AddInteraction(bool bInReregister = false)
	{
		return static_cast<T*>(AddInteraction(T::StaticClass(), bInReregister));
	}

	bool IsShiftDown() const
	{
		return bIsShiftDown;
	}

	bool IsAltDown() const
	{
		return bIsAltDown;
	}

	bool IsCtrlDown() const
	{
		return bIsCtrlDown;
	}

	bool IsLeftMouseButtonDown() const
	{
		return bIsLeftMouseButtonDown;
	}

	bool IsMiddleMouseButtonDown() const
	{
		return bIsMiddleMouseButtonDown;
	}

	bool IsRightMouseButtonDown() const
	{
		return bIsRightMouseButtonDown;
	}

	bool IsMouseLooking() const;

	void SetIsMouseLooking(bool bInIsLooking);

	FEditorViewportClient* GetEditorViewportClient() const;

	/**
	 * Marks the specified interaction type as active (meaning a viewport interaction of that type is currently active)
	 */
	void SetViewportInteractionActive(FName InViewportInteraction, bool bInActive);

	/**
	 * Checks if there is any active viewport interaction of the specified type
	 */
	bool EDITORINTERACTIVETOOLSFRAMEWORK_API IsViewportInteractionActive(FName InViewportInteraction);

	FOnMouseLookingChanged& OnMouseLookingStateChanged()
	{
		return OnMouseLookingChangedDelegate;
	}

private:
	/**
	 * All Input Behaviors from registered Viewport Interactions should be added to this Behavior Set (done with ::AddInteraction() )
	 */
	UPROPERTY(Transient)
	TObjectPtr<UInputBehaviorSet> BehaviorSet;

	/**
	 * A collection of ITF-based Viewport Interactions
	 * Key is the interaction name (class name when no Interaction name is specified)
	 */
	UPROPERTY(Transient)
	TMap<FName, TObjectPtr<UViewportInteraction>> ViewportInteractions;

	bool bIsShiftDown;
	bool bIsCtrlDown;
	bool bIsAltDown;

	bool bIsLeftMouseButtonDown;
	bool bIsMiddleMouseButtonDown;
	bool bIsRightMouseButtonDown;

	EMouseCursor::Type CursorOverride = EMouseCursor::None;

	UPROPERTY(Transient)
	TWeakObjectPtr<const UEditorInteractiveToolsContext> EditorInteractiveToolsContextWeak;

	/**
	 * Keeping track of active Viewport Interactions, so that FEditorViewportClient can disable legacy behaviors
	 * accordingly e.g. if viewport interactions for zooming are enabled, skip the legacy ones doing the same
	 */
	TMap<FName, bool> ViewportInteractionsStatusMap;

	FOnMouseLookingChanged OnMouseLookingChangedDelegate;
};

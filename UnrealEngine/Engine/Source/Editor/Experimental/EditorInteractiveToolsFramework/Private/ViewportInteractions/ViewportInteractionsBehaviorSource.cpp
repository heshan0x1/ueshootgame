// Copyright Epic Games, Inc. All Rights Reserved.

#include "ViewportInteractions/ViewportInteractionsBehaviorSource.h"
#include "BaseBehaviors/KeyAsModifierInputBehavior.h"
#include "EditorModeManager.h"
#include "EditorViewportClient.h"
#include "Tools/EdModeInteractiveToolsContext.h"
#include "UnrealClient.h"
#include "ViewportClientNavigationHelper.h"
#include "ViewportInteractions/ViewportInteraction.h"

namespace UE::Editor::ViewportInteractions
{
// CVar initializer
static int32 UseITFViewportInteractions = 0;
static FAutoConsoleVariableRef CVarEnableITFViewportInteractions(
	TEXT("ViewportInteractions.EnableITFInteractions"),
	UseITFViewportInteractions,
	TEXT("Are ITF Viewport Interactions enabled?"),
	FConsoleVariableDelegate::CreateLambda(
		[](const IConsoleVariable* InVariable)
		{
			ToggleEditorViewportInteractions(UseITFViewportInteractions == 1);
		}
	)
);

// CVar initializer
static int32 VerboseITFViewportInteractions = 1;
static FAutoConsoleVariableRef CVarEnableVerboseITFViewportInteractions(
	TEXT("ViewportInteractions.Verbose"),
	VerboseITFViewportInteractions,
	TEXT("Enables verbose logging for ITF Viewport Interactions"),
	FConsoleVariableDelegate::CreateLambda(
		[](const IConsoleVariable* InVariable)
		{
		}
	)
);

bool UseEditorViewportInteractions()
{
	return UseITFViewportInteractions == 1;
}

bool IsVerbose()
{
	return VerboseITFViewportInteractions == 1;
}

void ToggleEditorViewportInteractions(bool bInEnable)
{
	UseITFViewportInteractions = bInEnable;
	if (UseITFViewportInteractions)
	{
		OnEditorViewportInteractionsActivated().Broadcast();
	}
	else
	{
		OnEditorViewportInteractionsDeactivated().Broadcast();
	}
}

FOnEditorViewportInteractionsToggleDelegate& OnEditorViewportInteractionsActivated()
{
	static FOnEditorViewportInteractionsToggleDelegate OnViewportInteractionsActivated;
	return OnViewportInteractionsActivated;
}

FOnEditorViewportInteractionsToggleDelegate& OnEditorViewportInteractionsDeactivated()
{
	static FOnEditorViewportInteractionsToggleDelegate OnViewportInteractionsDeactivated;
	return OnViewportInteractionsDeactivated;
}

bool CommandMatchesKey(const TSharedPtr<FUICommandInfo>& InCommandInfo, const FKey& InKeyID)
{
	for (int32 i = 0; i < static_cast<uint8>(EMultipleKeyBindingIndex::NumChords); ++i)
	{
		EMultipleKeyBindingIndex ChordIndex = static_cast<EMultipleKeyBindingIndex>(i);
		if (InCommandInfo->GetActiveChord(ChordIndex)->Key == InKeyID)
		{
			return true;
		}
	}

	return false;
}

void LOG(const TCHAR* InMessage)
{
	if (IsVerbose())
	{
		UE_LOG(LogTemp, Log, TEXT("%s"), InMessage);
	}
}

static constexpr int MODIFIERS_PRIORITY = VIEWPORT_INTERACTIONS_HIGH_PRIORITY;

// We use this string to group Behavior Inputs so that we can selectively remove them if needed, without touching other inputs we might add (e.g. the ones added by Initialize)
static const FString ViewportInteractionsGroupName = TEXT("ViewportInteractions");

} // namespace UE::Editor::ViewportInteractions

void UViewportInteractionsBehaviorSource::OnUpdateModifierState(int InModifierID, bool bInIsOn)
{
	if (InModifierID == UE::Editor::ViewportInteractions::ShiftKeyMod)
	{
		bIsShiftDown = bInIsOn;
	}
	else if (InModifierID == UE::Editor::ViewportInteractions::CtrlKeyMod)
	{
		bIsCtrlDown = bInIsOn;
	}
	else if (InModifierID == UE::Editor::ViewportInteractions::AltKeyMod)
	{
		bIsAltDown = bInIsOn;
	}
	else if (InModifierID == UE::Editor::ViewportInteractions::LeftMouseButtonMod)
	{
		bIsLeftMouseButtonDown = bInIsOn;
	}
	else if (InModifierID == UE::Editor::ViewportInteractions::MiddleMouseButtonMod)
	{
		bIsMiddleMouseButtonDown = bInIsOn;
	}
	else if (InModifierID == UE::Editor::ViewportInteractions::RightMouseButtonMod)
	{
		bIsRightMouseButtonDown = bInIsOn;
	}
}

void UViewportInteractionsBehaviorSource::OnForceEndCapture()
{
	bIsShiftDown = false;
	bIsCtrlDown = false;
	bIsAltDown = false;

	bIsLeftMouseButtonDown = false;
	bIsMiddleMouseButtonDown = false;
	bIsRightMouseButtonDown = false;

	SetIsMouseLooking(false);
}

void UViewportInteractionsBehaviorSource::Initialize(const UEditorInteractiveToolsContext* InInteractiveToolsContext)
{
	if (!InInteractiveToolsContext)
	{
		return;
	}

	EditorInteractiveToolsContextWeak = InInteractiveToolsContext;

	BehaviorSet = NewObject<UInputBehaviorSet>();

	// Modifiers handling, directly from "this"
	{
		UKeyAsModifierInputBehavior* KeyAsModifierInputBehavior = NewObject<UKeyAsModifierInputBehavior>();
		KeyAsModifierInputBehavior->Initialize(
			this, UE::Editor::ViewportInteractions::ShiftKeyMod, FInputDeviceState::IsShiftKeyDown
		);
		KeyAsModifierInputBehavior->Initialize(
			this, UE::Editor::ViewportInteractions::CtrlKeyMod, FInputDeviceState::IsCtrlKeyDown
		);
		KeyAsModifierInputBehavior->Initialize(
			this, UE::Editor::ViewportInteractions::AltKeyMod, FInputDeviceState::IsAltKeyDown
		);
		KeyAsModifierInputBehavior->SetDefaultPriority(UE::Editor::ViewportInteractions::MODIFIERS_PRIORITY);
		BehaviorSet->Add(KeyAsModifierInputBehavior, this);

		UMouseButtonAsModifierInputBehavior* MouseButtonAsModifierInputBehavior =
			NewObject<UMouseButtonAsModifierInputBehavior>();
		MouseButtonAsModifierInputBehavior->Initialize(
			this,
			UE::Editor::ViewportInteractions::LeftMouseButtonMod,
			[](const FInputDeviceState& InputState)
			{
				return InputState.Mouse.Left.bDown || InputState.Mouse.Left.bPressed;
			}
		);
		MouseButtonAsModifierInputBehavior->Initialize(
			this,
			UE::Editor::ViewportInteractions::MiddleMouseButtonMod,
			[](const FInputDeviceState& InputState)
			{
				return InputState.Mouse.Middle.bDown || InputState.Mouse.Middle.bPressed;
			}
		);
		MouseButtonAsModifierInputBehavior->Initialize(
			this,
			UE::Editor::ViewportInteractions::RightMouseButtonMod,
			[](const FInputDeviceState& InputState)
			{
				return InputState.Mouse.Right.bDown || InputState.Mouse.Right.bPressed;
			}
		);
		MouseButtonAsModifierInputBehavior->SetDefaultPriority(UE::Editor::ViewportInteractions::MODIFIERS_PRIORITY);
		BehaviorSet->Add(MouseButtonAsModifierInputBehavior, this);
	}
}

void UViewportInteractionsBehaviorSource::RegisterBehaviorSources()
{
	if (const UEditorInteractiveToolsContext* InteractiveToolsContext = EditorInteractiveToolsContextWeak.Get())
	{
		if (UInputRouter* InputRouter = InteractiveToolsContext->InputRouter)
		{
			InputRouter->RegisterSource(this);
		}
	}
}

void UViewportInteractionsBehaviorSource::DeregisterBehaviorSources()
{
	if (const UEditorInteractiveToolsContext* InteractiveToolsContext = EditorInteractiveToolsContextWeak.Get())
	{
		if (UInputRouter* InputRouter = InteractiveToolsContext->InputRouter)
		{
			InputRouter->DeregisterSource(this);
		}
	}
}

void UViewportInteractionsBehaviorSource::Tick(float InDeltaTime) const
{
	for (const TPair<FName, TObjectPtr<UViewportInteraction>>& Pair : ViewportInteractions)
	{
		if (UViewportInteraction* Interaction = Pair.Value)
		{
			Interaction->Tick(InDeltaTime);
		}
	}
}

void UViewportInteractionsBehaviorSource::AddInteractions(const TArray<const UClass*>& InInteractions, bool bInReregister)
{
	for (const UClass* InteractionClass : InInteractions)
	{
		AddInteraction(InteractionClass);
	}

	if (bInReregister)
	{
		// Refreshing registered behaviors
		DeregisterBehaviorSources();
		RegisterBehaviorSources();
	}
}

UViewportInteraction* UViewportInteractionsBehaviorSource::AddInteraction(const UClass* InInteractionClass, bool bInReregister)
{
	if (!BehaviorSet && EditorInteractiveToolsContextWeak.IsValid())
	{
		Initialize(EditorInteractiveToolsContextWeak.Get());
	}

	UViewportInteraction* Interaction = NewObject<UViewportInteraction>(GetTransientPackage(), InInteractionClass);
	Interaction->Initialize(this);

	ViewportInteractions.Add(Interaction->GetInteractionName(), Interaction);

	for (UInputBehavior* const InputBehavior : Interaction->GetInputBehaviors())
	{
		BehaviorSet->Add(InputBehavior, Interaction, UE::Editor::ViewportInteractions::ViewportInteractionsGroupName);
	}

	if (bInReregister)
	{
		// Refreshing registered behaviors
		DeregisterBehaviorSources();
		RegisterBehaviorSources();
	}

	return Interaction;
}

void UViewportInteractionsBehaviorSource::Reset()
{
	DeregisterBehaviorSources();
	ViewportInteractions.Empty();
	BehaviorSet->RemoveByGroup(UE::Editor::ViewportInteractions::ViewportInteractionsGroupName);
}

void UViewportInteractionsBehaviorSource::SetMouseCursorOverride(EMouseCursor::Type InMouseCursor)
{
	CursorOverride = InMouseCursor;
}

bool UViewportInteractionsBehaviorSource::IsMouseLooking() const
{
	if (FEditorViewportClient* const EditorViewportClient = GetEditorViewportClient())
	{
		return EditorViewportClient->GetViewportNavigationHelper()->bIsMouseLooking;
	}

	return false;
}

void UViewportInteractionsBehaviorSource::SetIsMouseLooking(bool bInIsLooking)
{
	if (FEditorViewportClient* const EditorViewportClient = GetEditorViewportClient())
	{
		if (bInIsLooking)
		{
			EditorViewportClient->SetRequiredCursorOverride(true, CursorOverride);
		}
		else
		{
			EditorViewportClient->SetRequiredCursorOverride(false);
		}

		EditorViewportClient->GetViewportNavigationHelper()->bIsMouseLooking = bInIsLooking;
	}

	OnMouseLookingStateChanged().Broadcast(bInIsLooking);
}

FEditorViewportClient* UViewportInteractionsBehaviorSource::GetEditorViewportClient() const
{
	FEditorViewportClient* EditorViewportClient = nullptr;

	if (const UEditorInteractiveToolsContext* const InteractiveToolsContext = EditorInteractiveToolsContextWeak.Get())
	{
		if (const FEditorModeTools* const ModeManager = InteractiveToolsContext->GetParentEditorModeManager())
		{
			EditorViewportClient = ModeManager->GetFocusedViewportClient();
		}

		if (!EditorViewportClient)
		{
			if (UInteractiveToolManager* const ToolManager = InteractiveToolsContext->ToolManager)
			{
				if (const IToolsContextQueriesAPI* const ContextQueriesAPI = ToolManager->GetContextQueriesAPI())
				{
					if (const FViewport* const Viewport = ContextQueriesAPI->GetFocusedViewport())
					{
						EditorViewportClient = static_cast<FEditorViewportClient*>(Viewport->GetClient());
					}
				}
			}
		}
	}

	return EditorViewportClient;
}

void UViewportInteractionsBehaviorSource::SetViewportInteractionActive(FName InViewportInteraction, bool bInActive)
{
	ViewportInteractionsStatusMap.Emplace(InViewportInteraction, bInActive);
}

bool UViewportInteractionsBehaviorSource::IsViewportInteractionActive(FName InViewportInteraction)
{
	if (ViewportInteractionsStatusMap.Contains(InViewportInteraction))
	{
		return ViewportInteractionsStatusMap[InViewportInteraction];
	}

	return false;
}

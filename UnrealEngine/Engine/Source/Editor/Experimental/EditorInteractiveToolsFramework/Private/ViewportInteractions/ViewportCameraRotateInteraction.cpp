// Copyright Epic Games, Inc. All Rights Reserved.

#include "ViewportInteractions/ViewportCameraRotateInteraction.h"
#include "BaseBehaviors/KeyInputBehavior.h"
#include "CameraController.h"
#include "EditorViewportClient.h"
#include "ViewportClientNavigationHelper.h"
#include "ViewportInteractions/ViewportInteractionsBehaviorSource.h"

UViewportCameraRotateInteraction::UViewportCameraRotateInteraction()
{
	InteractionName = TEXT("Camera Rotate");
	ToolType = UE::Editor::ViewportInteractions::CameraRotate;

	UKeyInputBehavior* KeyInputBehavior = NewObject<UKeyInputBehavior>();
	KeyInputBehavior->Initialize(this, GetKeys());
	KeyInputBehavior->SetDefaultPriority(UE::Editor::ViewportInteractions::VIEWPORT_INTERACTIONS_DEFAULT_PRIORITY);
	KeyInputBehavior->bRequireAllKeys = false;

	KeyInputBehavior->ModifierCheckFunc = [this](const FInputDeviceState& InputDeviceState)
	{
		bool bResult = true;

		if (InputDeviceState.InputDevice == EInputDevices::Keyboard)
		{
			using namespace UE::Editor::ViewportInteractions;

			const FKey& ActiveKey = InputDeviceState.Keyboard.ActiveKey.Button;

			// These commands should only be capture if a mouse button is down (not mapped by default)
			if (CommandMatchesKey(FViewportNavigationCommands::Get().RotateUp, ActiveKey)
				|| CommandMatchesKey(FViewportNavigationCommands::Get().RotateDown, ActiveKey)
				|| CommandMatchesKey(FViewportNavigationCommands::Get().RotateLeft, ActiveKey)
				|| CommandMatchesKey(FViewportNavigationCommands::Get().RotateRight, ActiveKey))
			{
				bResult = IsAnyMouseButtonDown();
			}
		}

		return bResult;
	};

	KeyInputBehaviorWeak = KeyInputBehavior;

	RegisterInputBehavior(KeyInputBehavior);
}

void UViewportCameraRotateInteraction::OnKeyPressed(const FKey& InKeyID)
{
	if (IsEnabled())
	{
		constexpr bool bIsPressed = true;
		UpdateKeyState(InKeyID, bIsPressed);
	}
}

void UViewportCameraRotateInteraction::OnKeyReleased(const FKey& InKeyID)
{
	if (IsEnabled())
	{
		constexpr bool bIsPressed = false;
		UpdateKeyState(InKeyID, bIsPressed);
	}
}

void UViewportCameraRotateInteraction::Tick(float InDeltaTime) const
{
	if (RotateYawImpulse != 0 || RotatePitchImpulse != 0)
	{
		if (FEditorViewportClient* EditorViewportClient = GetEditorViewportClient())
		{
			if (FViewportClientNavigationHelper* NavigationHelper = EditorViewportClient->GetViewportNavigationHelper())
			{
				NavigationHelper->ImpulseDataDelta.RotateYawImpulse += RotateYawImpulse;
				NavigationHelper->ImpulseDataDelta.RotatePitchImpulse += RotatePitchImpulse;
			}
		}
	}
}

void UViewportCameraRotateInteraction::UpdateKeyState(const FKey& InKeyID, bool bInIsPressed)
{
	using namespace UE::Editor::ViewportInteractions;

	constexpr float ImpulseValue = 1.0f;

	const bool bMouseButtonDownOrRelease = IsAnyMouseButtonDown() || !bInIsPressed;

	// Rotate Up/Down
	if (CommandMatchesKey(FViewportNavigationCommands::Get().RotateUp, InKeyID) && bMouseButtonDownOrRelease)
	{
		RotatePitchImpulse = bInIsPressed ? ImpulseValue : 0.0f;
	}
	else if (CommandMatchesKey(FViewportNavigationCommands::Get().RotateDown, InKeyID) && bMouseButtonDownOrRelease)
	{
		RotatePitchImpulse = bInIsPressed ? -ImpulseValue : 0.0f;
	}
	// Rotate Left/Right
	else if (CommandMatchesKey(FViewportNavigationCommands::Get().RotateLeft, InKeyID) && bMouseButtonDownOrRelease)
	{
		RotateYawImpulse = bInIsPressed ? -ImpulseValue : 0.0f;
	}
	else if (CommandMatchesKey(FViewportNavigationCommands::Get().RotateRight, InKeyID) && bMouseButtonDownOrRelease)
	{
		RotateYawImpulse = bInIsPressed ? ImpulseValue : 0.0f;
	}
}

TArray<FKey> UViewportCameraRotateInteraction::GetKeys() const
{
	TArray<FKey> Keys;
	for (int32 i = 0; i < static_cast<uint8>(EMultipleKeyBindingIndex::NumChords); ++i)
	{
		EMultipleKeyBindingIndex ChordIndex = static_cast<EMultipleKeyBindingIndex>(i);

		TArray<FKey> KeysFromChords;
		for (const TSharedPtr<FUICommandInfo>& Command : GetCommands())
		{
			KeysFromChords.Add(Command->GetActiveChord(ChordIndex)->Key);
		}

		Keys.Append(KeysFromChords);
	}

	return Keys;
}

void UViewportCameraRotateInteraction::OnCommandChordChanged()
{
	if (TStrongObjectPtr<UKeyInputBehavior> KeyInputBehaviorPinned = KeyInputBehaviorWeak.Pin())
	{
		KeyInputBehaviorPinned->Initialize(this, GetKeys());
	}
}

TArray<TSharedPtr<FUICommandInfo>> UViewportCameraRotateInteraction::GetCommands() const
{
	if (FViewportNavigationCommands::IsRegistered())
	{
		return { FViewportNavigationCommands::Get().RotateUp,
				 FViewportNavigationCommands::Get().RotateDown,
				 FViewportNavigationCommands::Get().RotateLeft,
				 FViewportNavigationCommands::Get().RotateRight };
	}
	return {};
}

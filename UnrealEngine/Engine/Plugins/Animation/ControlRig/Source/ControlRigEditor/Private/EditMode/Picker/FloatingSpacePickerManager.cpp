// Copyright Epic Games, Inc. All Rights Reserved.

#include "FloatingSpacePickerManager.h"

#include "SFloatingSpacePicker_Base.h"
#include "SFloatingSpacePicker_AssetEditor.h"
#include "SFloatingSpacePicker_LevelEditor.h"
#include "Misc/RigEditModeUtils.h"

namespace UE::ControlRigEditor
{
FFloatingSpacePickerManager::FFloatingSpacePickerManager(FControlRigEditMode& InOwningMode)
	: OwningMode(InOwningMode)
{}

FFloatingSpacePickerManager::~FFloatingSpacePickerManager()
{
	CloseSpacePicker();
}

void FFloatingSpacePickerManager::SummonSpacePickerAtCursor()
{
	// We could reuse the window but it's easier to just recreate the widget from scratch. The overhead is fine.
	CloseSpacePicker();

	if (const TSharedPtr<SFloatingSpacePicker_Base> SpacePicker = CreateSpacePicker())
	{
		WeakWindow = SpacePicker->ShowWindow();
	}
}

void FFloatingSpacePickerManager::CloseSpacePicker()
{
	if (const TSharedPtr<SWindow> WindowPin = WeakWindow.Pin())
	{
		WindowPin->RequestDestroyWindow();
		WeakWindow.Reset();
	}
}

TSharedPtr<SFloatingSpacePicker_Base> FFloatingSpacePickerManager::CreateSpacePicker()
{
	using namespace UE::ControlRigEditor;
	const FInitialSpacePickerSelection InitialSelection = DetermineInitialSpacePickerSelection(OwningMode);
	if (!InitialSelection.IsValid())
	{
		return nullptr;
	}

	if (OwningMode.AreEditingControlRigDirectly())
	{
		return SNew(SFloatingSpacePicker_AssetEditor)
			.SelectedControls(InitialSelection.SelectedControls)
			.DisplayedRig(InitialSelection.RuntimeRig);
	}
	else
	{
		TAttribute<TSharedPtr<ISequencer>> SequencerAttr = TAttribute<TSharedPtr<ISequencer>>::CreateLambda([this]
		{
			// OwningMode should be valid here because it is responsible for destroying us when it is destroyed.
			return OwningMode.GetWeakSequencer().Pin();
		});
		return SNew(SFloatingSpacePicker_LevelEditor, MoveTemp(SequencerAttr))
			.SelectedControls(InitialSelection.SelectedControls)
			.DisplayedRig(InitialSelection.RuntimeRig);
	}
}
}
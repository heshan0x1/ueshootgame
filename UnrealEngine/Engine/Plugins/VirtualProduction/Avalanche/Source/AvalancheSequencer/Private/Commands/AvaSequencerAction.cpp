// Copyright Epic Games, Inc. All Rights Reserved.

#include "AvaSequencerAction.h"
#include "AvaSequencer.h"
#include "MVVM/ViewModels/SequencerEditorViewModel.h"

TSharedPtr<UE::Sequencer::FSequencerSelection> FAvaSequencerAction::GetSequencerSelection() const
{
	using namespace UE::Sequencer;

	TSharedPtr<ISequencer> Sequencer = Owner.GetSequencerPtr();
	if (!Sequencer.IsValid())
	{
		return nullptr;
	}

	if (TSharedPtr<FSequencerEditorViewModel> EditorViewModel = Sequencer->GetViewModel())
	{
		return EditorViewModel->GetSelection();
	}

	return nullptr;
}

// Copyright Epic Games, Inc. All Rights Reserved.

#include "SFloatingSpacePicker_LevelEditor.h"

#include "ControlRigSpaceChannelEditors.h"
#include "ScopedTransaction.h"
#include "Editor/SRigSpacePickerWidget.h"
#include "Rigs/RigHierarchyController.h"

#define LOCTEXT_NAMESPACE "SFloatingSpacePicker"

namespace UE::ControlRigEditor
{
void SFloatingSpacePicker_LevelEditor::Construct(const FArguments& InArgs, TAttribute<TSharedPtr<ISequencer>> InSequencerAttr)
{
	SequencerAttr = MoveTemp(InSequencerAttr);
	
	constexpr bool bIsEditingDirectly = false;
	SFloatingSpacePicker_Base::Construct(InArgs, bIsEditingDirectly);
}

void SFloatingSpacePicker_LevelEditor::OnActiveSpaceChanged(
	URigHierarchy* InHierarchy, const FRigElementKey& InControlKey, const FRigElementKey& InSpaceKey
	)
{
	check(SelectedControls.Contains(InControlKey));

	UControlRig* RuntimeRig = WeakDisplayedRig.Get();
	if (!ensure(RuntimeRig))
	{
		return;
	}

	const TSharedPtr<ISequencer> Sequencer = SequencerAttr.Get();
	const FRigControlElement* ControlElement = InHierarchy->Find<FRigControlElement>(InControlKey);
	if (!Sequencer || !ControlElement)
	{
		return;
	}
	
	constexpr bool bDoNotCreate = false;
	FSpaceChannelAndSection SpaceChannelAndSection = FControlRigSpaceChannelHelpers::FindSpaceChannelAndSectionForControl(
		RuntimeRig, InControlKey.Name, Sequencer.Get(), bDoNotCreate
		);
	if (!SpaceChannelAndSection.SpaceChannel)
	{
		// no need to create a space channel if InSpaceKey is the current parent 
		if (InHierarchy->GetActiveParent(InControlKey) == InSpaceKey)
		{
			return;
		}
	}
					
	const FScopedTransaction Transaction(LOCTEXT("KeyControlRigSpace", "Key Control Rig Space"));
	constexpr bool bCreateIfNeeded = true;
	SpaceChannelAndSection = FControlRigSpaceChannelHelpers::FindSpaceChannelAndSectionForControl(
		RuntimeRig, InControlKey.Name, Sequencer.Get(), bCreateIfNeeded
		);
	if (SpaceChannelAndSection.SpaceChannel)
	{
		const FFrameRate TickResolution = Sequencer->GetFocusedTickResolution();
		const FFrameTime FrameTime = Sequencer->GetLocalTime().ConvertTo(TickResolution);
		FFrameNumber CurrentTime = FrameTime.GetFrame();
		FControlRigSpaceChannelHelpers::SequencerKeyControlRigSpaceChannel(
			RuntimeRig, Sequencer.Get(), SpaceChannelAndSection.SpaceChannel, SpaceChannelAndSection.SectionToKey, CurrentTime,
			InHierarchy, InControlKey, InSpaceKey
			);
	}
}

void SFloatingSpacePicker_LevelEditor::OnSpaceListChanged(
	URigHierarchy* InHierarchy, const FRigElementKey& InControlKey, const TArray<FRigElementKeyWithLabel>& InSpaceList
	)
{
	check(SelectedControls.Contains(InControlKey));

	UControlRig* RuntimeRig = WeakDisplayedRig.Get();
	if (!ensure(RuntimeRig))
	{
		return;
	}
		
	// update the settings in the control element
	if (FRigControlElement* ControlElement = InHierarchy->Find<FRigControlElement>(InControlKey))
	{
		FScopedTransaction Transaction(LOCTEXT("ControlChangeAvailableSpaces", "Edit Available Spaces"));

		InHierarchy->Modify();

		FRigControlElementCustomization ControlCustomization = *RuntimeRig->GetControlCustomization(InControlKey);	
		ControlCustomization.AvailableSpaces = InSpaceList;
		ControlCustomization.RemovedSpaces.Reset();

		// remember  the elements which are in the asset's available list but removed by the user
		for(const FRigElementKeyWithLabel& AvailableSpace : ControlElement->Settings.Customization.AvailableSpaces)
		{
			if(ControlCustomization.AvailableSpaces.FindByKey(AvailableSpace.Key) == nullptr)
			{
				ControlCustomization.RemovedSpaces.Add(AvailableSpace.Key);
			}
		}

		RuntimeRig->SetControlCustomization(InControlKey, ControlCustomization);
		InHierarchy->Notify(ERigHierarchyNotification::ControlSettingChanged, ControlElement);
	}
}
}

#undef LOCTEXT_NAMESPACE
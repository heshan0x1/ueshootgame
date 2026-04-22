// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ControlRig.h"
#include "SFloatingSpacePicker_Base.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class FControlRigEditMode;
class ISequencer;
class SRigSpacePickerWidget;

namespace UE::ControlRigEditor
{
/**
 * Handles displaying the floating space picker when indirectly editing control rigs,
 * i.e. when FControlRigEditMode::AreEditingControlRigDirectly() == false.
 */
class SFloatingSpacePicker_LevelEditor : public SFloatingSpacePicker_Base
{
public:
	
	void Construct(const FArguments& InArgs, TAttribute<TSharedPtr<ISequencer>> InSequencerAttr);

private:

	/** The level editor's Sequencer. */
	TAttribute<TSharedPtr<ISequencer>> SequencerAttr;

	virtual void OnActiveSpaceChanged(URigHierarchy* InHierarchy, const FRigElementKey& InControlKey, const FRigElementKey& InSpaceKey) override;
	virtual void OnSpaceListChanged(URigHierarchy* InHierarchy, const FRigElementKey& InControlKey, const TArray<FRigElementKeyWithLabel>& InSpaceList) override;
};
}



// Copyright Epic Games, Inc. All Rights Reserved.

/**
* Space Picker View
*/
#pragma once

#include "CoreMinimal.h"
#include "EditMode/ControlRigBaseDockableView.h"
#include "Widgets/SWidget.h"
#include "Widgets/SCompoundWidget.h"
#include "Misc/FrameNumber.h"
#include "Editor/SRigSpacePickerWidget.h"

class ISequencer;
class SExpandableArea;
class SSearchableRigHierarchyTreeView;
class UControlRig;
namespace UE::ControlRigEditor { class FRigSelectionViewModel; }

class SControlRigSpacePicker : public SCompoundWidget, public FControlRigBaseDockableView
{

	SLATE_BEGIN_ARGS(SControlRigSpacePicker)
		: _InitiallyCollapsed(true)
	{}
		/** Whether or not the area is initially collapsed */
		SLATE_ARGUMENT(bool, InitiallyCollapsed)
		/** The initially selected hierarchy. */
		SLATE_ARGUMENT(URigHierarchy*, Hierarchy)
		/** The initially selected controls. */
		SLATE_ARGUMENT(TArray<FRigElementKey>, Controls)
	SLATE_END_ARGS()

	virtual ~SControlRigSpacePicker() override;

	void Construct(
		const FArguments& InArgs, 
		FControlRigEditMode& InEditMode, const TSharedRef<UE::ControlRigEditor::FRigSelectionViewModel>& InSelectionViewModel
		);

private:
	
	/** View model for when control rig selection changes. */
	TSharedPtr<UE::ControlRigEditor::FRigSelectionViewModel> SelectionViewModel;
	
	virtual void HandleControlSelected(UControlRig* Subject, FRigControlElement* InControl, bool bSelected) override;

	/** Space picker widget*/
	TSharedPtr<SRigSpacePickerWidget> SpacePickerWidget;
	TSharedPtr<SExpandableArea> PickerExpander;

	const FRigControlElementCustomization* HandleGetControlElementCustomization(URigHierarchy* InHierarchy, const FRigElementKey& InControlKey);
	void HandleActiveSpaceChanged(URigHierarchy* InHierarchy, const FRigElementKey& InControlKey, const FRigElementKey& InSpaceKey);
	void HandleSpaceListChanged(URigHierarchy* InHierarchy, const FRigElementKey& InControlKey, const TArray<FRigElementKeyWithLabel>& InSpaceList);
	FReply OnBakeControlsToNewSpaceButtonClicked();
	FReply OnCompensateKeyClicked();
	FReply OnCompensateAllClicked();
	void Compensate(TOptional<FFrameNumber> OptionalKeyTime, bool bSetPreviousTick);
	EVisibility GetAddSpaceButtonVisibility() const;
	virtual TSharedRef<FControlRigBaseDockableView> AsSharedWidget() override { return SharedThis(this); }

	bool ReadyForBakeOrCompensation() const;
	//for now picker works off of one ControlRig, this function gets the first control rig with a selection
	UControlRig* GetControlRig() const;

};


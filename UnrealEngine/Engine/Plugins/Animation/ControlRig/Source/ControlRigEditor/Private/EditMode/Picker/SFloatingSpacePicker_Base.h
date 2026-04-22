// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ControlRig.h"
#include "Editor/SRigSpacePickerWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class FControlRigEditMode;
class SRigSpacePickerWidget;

namespace UE::ControlRigEditor
{
/**
 * Handles displaying the floating space picker when directly editing control rigs,
 * i.e. when FControlRigEditMode::IsEditingControlRigDirectly() == true.
 */
class SFloatingSpacePicker_Base
	// Initially this was SCompoundWidget and created SRigSpacePickerWidget as child widget. Won't work.
	// We need to inherit because SRigSpacePickerWidget::OpenDialog uses AsShared to put itself into the SWindow; but then the parent widget, which is
	// this SFloatingSpacePicker_Base, would wind up unreferenced and destroyed.
	// SRigSpacePickerWidget::OpenDialog should be adjusted to work with composed views by extracting it from SRigSpacePickerWidget... that would allow
	// us to pass in the widget that should be in the window.
	: public SRigSpacePickerWidget
{
public:

	SLATE_BEGIN_ARGS(SFloatingSpacePicker_Base){}
		/** Required. The controls to affect. */
		SLATE_ARGUMENT(TArray<FRigElementKey>, SelectedControls)
		/** Required. The rig that we're picking for. */
		SLATE_ARGUMENT(UControlRig*, DisplayedRig)
	SLATE_END_ARGS()
	
	void Construct(const FArguments& InArgs, bool bIsEditingDirectly);

	/** Places this widget in a window and shows it. */
	TSharedPtr<SWindow> ShowWindow();

protected:

	/** The rig being displayed */
	TWeakObjectPtr<UControlRig> WeakDisplayedRig = nullptr;
	/** The controls being displayed. */
	TArray<FRigElementKey> SelectedControls;

	virtual void OnActiveSpaceChanged(URigHierarchy* InHierarchy, const FRigElementKey& InControlKey, const FRigElementKey& InSpaceKey) = 0;
	virtual void OnSpaceListChanged(URigHierarchy* InHierarchy, const FRigElementKey& InControlKey, const TArray<FRigElementKeyWithLabel>& InSpaceList) = 0;
};
}



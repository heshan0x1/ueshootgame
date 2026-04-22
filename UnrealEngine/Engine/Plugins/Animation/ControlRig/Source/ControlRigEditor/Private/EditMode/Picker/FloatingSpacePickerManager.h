// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EditMode/ControlRigEditMode.h"
#include "Templates/UnrealTemplate.h"

namespace UE::ControlRigEditor
{
}

namespace UE::ControlRigEditor
{
class SFloatingSpacePicker_Base;
	
/** Manages the floating space picker, which by default is spawned by pressing tab. */
class FFloatingSpacePickerManager : public FNoncopyable
{
public:

	explicit FFloatingSpacePickerManager(FControlRigEditMode& InOwningMode UE_LIFETIMEBOUND);
	~FFloatingSpacePickerManager();

	/** Spawns a new space picker at the cursor. Replaces the old one if one exists. */
	void SummonSpacePickerAtCursor();

	/** Closes the space picker if it currently is being shown. */
	void CloseSpacePicker();

private:

	/** The edit mode that owns us. Needed to display some information about the space picker. */
	FControlRigEditMode& OwningMode;

	/** The window that contained the last created space picker. If still valid, we replace the window. */
	TWeakPtr<SWindow> WeakWindow;

	/** Creates the space picker depending on the FControlRigEditMode::AreEditingControlRigDirectly. */
	TSharedPtr<SFloatingSpacePicker_Base> CreateSpacePicker();
};
}


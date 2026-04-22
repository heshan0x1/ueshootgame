// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Array.h"

class FControlRigEditMode;
class UControlRig;
class URigHierarchy;
struct FRigElementKey;

namespace UE::ControlRigEditor
{
struct FInitialSpacePickerSelection
{
	/** The controls selected on the rig. */
	TArray<FRigElementKey> SelectedControls;
	/** The selected rig */
	UControlRig* RuntimeRig = nullptr;
	/** Hierarchy of RuntimeRig */
	URigHierarchy* Hierarchy = nullptr;
	
	bool IsValid() const { return Hierarchy && !SelectedControls.IsEmpty(); }  
};

/** @return The initial hierarchy and controls to select when constructing SRigSpacePickerWidget. */
FInitialSpacePickerSelection DetermineInitialSpacePickerSelection(FControlRigEditMode& InEditMode);
}

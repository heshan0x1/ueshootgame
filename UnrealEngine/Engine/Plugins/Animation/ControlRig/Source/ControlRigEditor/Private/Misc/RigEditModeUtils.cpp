// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigEditModeUtils.h"

#include "ControlRig.h"
#include "EditMode/ControlRigEditMode.h"

namespace UE::ControlRigEditor
{
FInitialSpacePickerSelection DetermineInitialSpacePickerSelection(FControlRigEditMode& InEditMode)
{
	TMap<UControlRig*, TArray<FRigElementKey>> SelectedControlRigsAndControls;
	InEditMode.GetAllSelectedControls(SelectedControlRigsAndControls);
	
	TArray<UControlRig*> ControlRigs;
	TArray<TArray<FRigElementKey>> AllSelectedControls;
	SelectedControlRigsAndControls.GenerateKeyArray(ControlRigs);
	SelectedControlRigsAndControls.GenerateValueArray(AllSelectedControls);
	
	//mz todo handle multiple control rigs with space picker
	UControlRig* RuntimeRig = ControlRigs.IsEmpty() ? nullptr : ControlRigs[0];
	TArray<FRigElementKey> SelectedControls = AllSelectedControls.IsEmpty() ? TArray<FRigElementKey>() : MoveTemp(AllSelectedControls[0]);
	URigHierarchy* Hierarchy = RuntimeRig ? RuntimeRig->GetHierarchy() : nullptr;

	return FInitialSpacePickerSelection{ MoveTemp(SelectedControls), RuntimeRig, Hierarchy };
}
}

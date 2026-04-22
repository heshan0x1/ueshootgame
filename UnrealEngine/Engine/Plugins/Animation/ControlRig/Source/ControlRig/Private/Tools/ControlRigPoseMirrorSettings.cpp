// Copyright Epic Games, Inc. All Rights Reserved.
#include "Tools/ControlRigPoseMirrorSettings.h"

#include "Tools/ControlRigPoseMirrorTable.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(ControlRigPoseMirrorSettings)

UControlRigPoseMirrorSettings::UControlRigPoseMirrorSettings()
{
	MirrorAxis = EAxis::X;
	AxisToFlip = EAxis::X;
	MirrorMatchTolerance = .2;
}

#if WITH_EDITOR
//property changed so reset the cached mirror tables
void UControlRigPoseMirrorSettings::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	FControlRigPoseMirrorTable MirrorTable;
	MirrorTable.Reset();
	SaveConfig();
}
#endif

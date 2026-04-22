// Copyright Epic Games, Inc. All Rights Reserved.

#include "EvaluationVM/Tasks/NormalizeRotations.h"

#include "EvaluationVM/EvaluationVM.h"
#include "EvaluationVM/KeyframeState.h"
#include "TransformArrayOperations.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(NormalizeRotations)

void FAnimNextNormalizeKeyframeRotationsTask::Execute(UE::UAF::FEvaluationVM& VM) const
{
	using namespace UE::UAF;

	if (EnumHasAnyFlags(VM.GetFlags(), EEvaluationFlags::Bones))
	{
		TUniquePtr<FKeyframeState> Keyframe;
		if (!VM.PopValue(KEYFRAME_STACK_NAME, Keyframe))
		{
			// We have no inputs, nothing to do
			return;
		}

		NormalizeRotations(Keyframe->Pose.LocalTransforms.GetView());

		VM.PushValue(KEYFRAME_STACK_NAME, MoveTemp(Keyframe));
	}
}

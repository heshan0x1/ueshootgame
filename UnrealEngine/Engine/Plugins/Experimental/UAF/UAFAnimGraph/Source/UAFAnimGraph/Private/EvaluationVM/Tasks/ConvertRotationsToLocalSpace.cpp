// Copyright Epic Games, Inc. All Rights Reserved.

#include "EvaluationVM/Tasks/ConvertRotationsToLocalSpace.h"

#include "EvaluationVM/EvaluationVM.h"
#include "EvaluationVM/Tasks/ConvertRotationSpaceDefs.h"

#if UE_ANIM_CONVERT_ROTATION_SPACE_TASKS_ISPC
#include "ConvertRotationSpace.ispc.generated.h"
#endif

#include UE_INLINE_GENERATED_CPP_BY_NAME(ConvertRotationsToLocalSpace)

FAnimNextConvertRotationsToLocalSpaceTask FAnimNextConvertRotationsToLocalSpaceTask::Make(const int32 NumPoses)
{
	FAnimNextConvertRotationsToLocalSpaceTask Task;
	Task.NumPoses = NumPoses;

	return Task;
}

void FAnimNextConvertRotationsToLocalSpaceTask::Execute(UE::UAF::FEvaluationVM& VM) const
{
	using namespace UE::UAF;

	if (!EnumHasAnyFlags(VM.GetFlags(), EEvaluationFlags::Bones))
	{
		return;
	}

	for (int32 PoseIndex = 0; PoseIndex < NumPoses; ++PoseIndex)
	{
		TUniquePtr<FKeyframeState>* Keyframe = VM.PeekValueMutable<TUniquePtr<FKeyframeState>>(KEYFRAME_STACK_NAME, PoseIndex);
		if (!Keyframe || !Keyframe->IsValid())
		{
			// No pose to convert
			return;
		}

		FLODPoseStack& Pose = (*Keyframe)->Pose;
		TArrayView<FQuat> PoseRotations = Pose.LocalTransforms.Rotations;

#if UE_ANIM_CONVERT_ROTATION_SPACE_TASKS_ISPC
		static_assert(sizeof(FBoneIndexType) == sizeof(uint16)); // ISPC needs updating if assert fails

		const TArrayView<const FBoneIndexType> ParentIndices = Pose.GetRefPose().GetLODBoneIndexToParentLODBoneIndexMap(Pose.LODLevel);

		ispc::ConvertRotationsToLocalSpace(
			reinterpret_cast<ispc::FVector4*>(PoseRotations.GetData()),
			reinterpret_cast<const uint16*>(ParentIndices.GetData()),
			Pose.GetNumBones());
#else
		for (FBoneIndexType Index = PoseRotations.Num() - 1; Index > 0; --Index)
		{
			const FBoneIndexType ParentIndex = Pose.GetLODBoneParentIndex(Index);
			PoseRotations[Index] = PoseRotations[ParentIndex].Inverse() * PoseRotations[Index];
		}
#endif // UE_ANIM_CONVERT_ROTATION_SPACE_TASKS_ISPC
	}
}

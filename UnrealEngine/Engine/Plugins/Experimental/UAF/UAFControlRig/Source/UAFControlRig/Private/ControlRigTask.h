// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EvaluationVM/EvaluationTask.h"
#include "Animation/AnimCurveTypes.h"
#include "LODPose.h"
#include "ControlRigTrait.h"
#include "AnimNextStats.h"
// --- ---
#include "ControlRigTask.generated.h"

namespace UE::UAF
{
struct FKeyframeState;
}

DECLARE_CYCLE_STAT_EXTERN(TEXT("UAF Task: ControlRig"), STAT_AnimNext_Task_ControlRig, STATGROUP_AnimNext, UAFCONTROLRIG_API);

USTRUCT()
struct UAFCONTROLRIG_API FAnimNextControlRigTask : public FAnimNextEvaluationTask
{
	GENERATED_BODY()

	DECLARE_ANIM_EVALUATION_TASK(FAnimNextControlRigTask)

	static FAnimNextControlRigTask Make(const UE::UAF::FControlRigTrait::FSharedData* SharedData, UE::UAF::FControlRigTrait::FInstanceData* InstanceData);

	// Task entry point
	virtual void Execute(UE::UAF::FEvaluationVM& VM) const override;

private:
	void ExecuteControlRig(UE::UAF::FEvaluationVM& VM, UE::UAF::FKeyframeState& KeyFrameState, UControlRig* ControlRig) const;
	void UpdateInput(UE::UAF::FEvaluationVM& VM, UE::UAF::FKeyframeState& KeyFrameState, UControlRig* ControlRig, UE::UAF::FKeyframeState& InOutput) const;
	void UpdateOutput(UE::UAF::FEvaluationVM& VM, UE::UAF::FKeyframeState& KeyFrameState, UControlRig* ControlRig, UE::UAF::FKeyframeState& InOutput) const;

	bool CanExecute(const UControlRig* ControlRig) const;

	void QueueControlRigDrawInstructions(UControlRig* ControlRig, FRigVMDrawInterface* DebugDrawInterface, const FTransform& ComponentTransform) const;

	const UE::UAF::FControlRigTrait::FSharedData* SharedData = nullptr;
	UE::UAF::FControlRigTrait::FInstanceData* InstanceData = nullptr;
};

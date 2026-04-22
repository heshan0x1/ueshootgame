// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AnimNextExecuteContext.h"
#include "Graph/RigUnit_AnimNextBase.h"
#include "Animation/TrajectoryTypes.h"

#include "RigUnit_DebugDrawTrajectory.generated.h"

USTRUCT(meta = (DisplayName = "Debug Draw Trajectory", Category = "Trajectory", NodeColor = "0, 1, 1", Keywords = "Mover, Trajectory, Debug"))
struct FRigUnit_DebugDrawTrajectory: public FRigUnit_AnimNextBase
{
	GENERATED_BODY()

	RIGVM_METHOD()
	void Execute();

	UPROPERTY(EditAnywhere, Category = "Debug Draw - Trajectory", meta = (Input, Output))
	FTransformTrajectory Trajectory;

	UPROPERTY(EditAnywhere, Category = "Debug Draw - Trajectory", meta = (Input))
	float DebugThickness = 0.f;

	UPROPERTY(EditAnywhere, Category = "Debug Draw - Trajectory", meta = (Input))
	float DebugOffset = 0.f;

	UPROPERTY(EditAnywhere, Category = "Debug Draw - Trajectory", meta = (Input))
	bool Enabled = false;

	// The execution result
	UPROPERTY(EditAnywhere, DisplayName = "Execute", Category = "BeginExecution", meta = (Input, Output))
	FAnimNextExecuteContext ExecuteContext;
};

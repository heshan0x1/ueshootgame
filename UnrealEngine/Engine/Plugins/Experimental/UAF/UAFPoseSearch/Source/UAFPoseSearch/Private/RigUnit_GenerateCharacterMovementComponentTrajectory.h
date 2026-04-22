// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AnimNextExecuteContext.h"
#include "Graph/RigUnit_AnimNextBase.h"
#include "Animation/TrajectoryTypes.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "PoseSearch/PoseSearchTrajectoryLibrary.h"

#include "RigUnit_GenerateCharacterMovementComponentTrajectory.generated.h"

USTRUCT(meta = (DisplayName = "Generate Trajectory from Character Movement Component", Category = "Character Movement", NodeColor = "0, 1, 1", Keywords = "Trajectory"))
struct FRigUnit_GenerateCharacterMovementComponentTrajectory : public FRigUnit_AnimNextBase
{
	GENERATED_BODY()

	RIGVM_METHOD()
	void Execute();

	UPROPERTY(EditAnywhere, Category = "Trajectory", meta = (Input))
	TObjectPtr<UCharacterMovementComponent> CharacterMovementComponent;

	UPROPERTY(EditAnywhere, Category = "Trajectory", meta = (Input))
	// This should be the most recent simulation time that was used to get us to our current state
	float DeltaTime = 0.f;

	UPROPERTY(EditAnywhere, Category = "Trajectory", meta = (Input))
	float HistorySamplingInterval = -1.f;

	UPROPERTY(EditAnywhere, Category = "Trajectory", meta = (Input))
	int32 NumHistorySamples = 30;

	UPROPERTY(EditAnywhere, Category = "Trajectory", meta = (Input))
	float PredictionSamplingInterval = 0.1f;

	UPROPERTY(EditAnywhere, Category = "Trajectory", meta = (Input))
	int32 NumPredictionSamples = 15;

	UPROPERTY(EditAnywhere, Category = "Trajectory", meta = (Input))
	FPoseSearchTrajectoryData TrajectoryData;
	
	UPROPERTY(EditAnywhere, Category = "Trajectory", meta = (Input, Output))
	FTransformTrajectory InOutTrajectory;

	UPROPERTY(EditAnywhere, Category = "Trajectory", meta = (Input, Output))
	float InOutDesiredControllerYawLastUpdate = 0.0f;
	
	// The execution result
	UPROPERTY(EditAnywhere, DisplayName = "Execute", Category = "BeginExecution", meta = (Input, Output))
	FAnimNextExecuteContext ExecuteContext;
};

// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigUnit_GenerateCharacterMovementComponentTrajectory.h"

#include "GameFramework/CharacterMovementComponent.h"
#include "PoseSearch/PoseSearchDefines.h"
#include "PoseSearch/PoseSearchTrajectoryLibrary.h"

FRigUnit_GenerateCharacterMovementComponentTrajectory_Execute()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FRigUnit_GenerateCharacterMovementComponentTrajectory_Execute);
	using namespace UE::UAF;

	if (CharacterMovementComponent == nullptr)
	{
		return;
	}

	AActor* Owner = CharacterMovementComponent->GetOwner();
	if (Owner == nullptr)
	{
		UE_LOG(LogPoseSearch, Warning, TEXT("FRigUnit_GenerateCharacterMovementComponentTrajectory_Execute: Failed to get Character Movement Component's actor owner."));
		return;
	}
	
	UPoseSearchTrajectoryLibrary::PoseSearchGenerateTransformTrajectory(
		Owner,
		TrajectoryData,
		DeltaTime,
		InOutTrajectory,
		InOutDesiredControllerYawLastUpdate,
		InOutTrajectory,
		HistorySamplingInterval,
		NumHistorySamples,
		PredictionSamplingInterval,
		NumPredictionSamples
	);
}

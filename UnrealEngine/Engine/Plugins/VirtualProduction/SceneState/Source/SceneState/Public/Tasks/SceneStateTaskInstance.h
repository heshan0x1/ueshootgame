// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SceneStateEnums.h"
#include "SceneStateTaskInstance.generated.h"

struct FPropertyBindingDataView;

/** Represents the Instance Data of a Task */
USTRUCT()
struct FSceneStateTaskInstance
{
	GENERATED_BODY()

	/** Sets the execution status of the Task */
	void SetStatus(UE::SceneState::EExecutionStatus InStatus);

	/** Gets the execution status of the task */
	UE::SceneState::EExecutionStatus GetStatus() const
	{
		return Status;
	}

private:
	/** Current status of the Task */
	UE::SceneState::EExecutionStatus Status = UE::SceneState::EExecutionStatus::NotStarted;
};

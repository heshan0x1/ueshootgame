// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SceneStateEnums.h"
#include "SceneStateInstance.generated.h"

/** Instance data of a State */
USTRUCT()
struct FSceneStateInstance
{
	GENERATED_BODY()

	SCENESTATE_API FSceneStateInstance();

	/** Time elapsed since the state entered */
	float ElapsedTime = 0.f;

	/** Current status of this state instance */
	UE::SceneState::EExecutionStatus Status = UE::SceneState::EExecutionStatus::NotStarted;

	uint16 GetInstanceId() const;

private:
	/** The id for this instance. This is used to differentiate state instances for a same state and consequently task instances for a same task*/
	uint16 InstanceId;
};

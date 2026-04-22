// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tasks/SceneStateTaskInstance.h"

void FSceneStateTaskInstance::SetStatus(UE::SceneState::EExecutionStatus InStatus)
{
	Status = InStatus;
}

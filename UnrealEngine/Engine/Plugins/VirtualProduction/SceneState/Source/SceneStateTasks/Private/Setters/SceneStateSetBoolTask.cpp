// Copyright Epic Games, Inc. All Rights Reserved.

#include "Setters/SceneStateSetBoolTask.h"
#include "Setters/SceneStateSetterUtils.h"

#if WITH_EDITOR
const UScriptStruct* FSceneStateSetBoolTask::OnGetTaskInstanceType() const
{
	return FInstanceDataType::StaticStruct();
}
#endif

void FSceneStateSetBoolTask::OnStart(const FSceneStateExecutionContext& InContext, FStructView InTaskInstance) const
{
	UE::SceneState::SetValue<bool>(*this, InContext, InTaskInstance);
	Finish(InContext, InTaskInstance);
}

// Copyright Epic Games, Inc. All Rights Reserved.

#include "Setters/SceneStateSetFloatTask.h"
#include "Setters/SceneStateSetterUtils.h"

#if WITH_EDITOR
const UScriptStruct* FSceneStateSetFloatTask::OnGetTaskInstanceType() const
{
	return FInstanceDataType::StaticStruct();
}
#endif

void FSceneStateSetFloatTask::OnStart(const FSceneStateExecutionContext& InContext, FStructView InTaskInstance) const
{
	UE::SceneState::SetValue<double, float>(*this, InContext, InTaskInstance);
	Finish(InContext, InTaskInstance);
}

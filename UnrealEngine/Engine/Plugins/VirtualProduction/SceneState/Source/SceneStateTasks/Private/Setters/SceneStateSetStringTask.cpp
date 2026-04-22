// Copyright Epic Games, Inc. All Rights Reserved.

#include "Setters/SceneStateSetStringTask.h"
#include "Setters/SceneStateSetterUtils.h"

#if WITH_EDITOR
const UScriptStruct* FSceneStateSetStringTask::OnGetTaskInstanceType() const
{
	return FInstanceDataType::StaticStruct();
}
#endif

void FSceneStateSetStringTask::OnStart(const FSceneStateExecutionContext& InContext, FStructView InTaskInstance) const
{
	UE::SceneState::SetValue<FString>(*this, InContext, InTaskInstance);
	Finish(InContext, InTaskInstance);
}

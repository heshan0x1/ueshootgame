// Copyright Epic Games, Inc. All Rights Reserved.

#include "Functions/SceneStateFromStringFunctions.h"

void FSceneStateTextFromStringFunction::OnExecute(const FSceneStateExecutionContext& InContext, FStructView InFunctionInstance) const
{
	FInstanceDataType& InstanceData = InFunctionInstance.Get<FInstanceDataType>();
	InstanceData.Output = FText::FromString(InstanceData.String);
}

void FSceneStateNameFromStringFunction::OnExecute(const FSceneStateExecutionContext& InContext, FStructView InFunctionInstance) const
{
	FInstanceDataType& InstanceData = InFunctionInstance.Get<FInstanceDataType>();
	InstanceData.Output = FName(InstanceData.String);
}

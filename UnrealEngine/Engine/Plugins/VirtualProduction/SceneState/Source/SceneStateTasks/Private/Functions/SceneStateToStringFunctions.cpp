// Copyright Epic Games, Inc. All Rights Reserved.

#include "Functions/SceneStateToStringFunctions.h"

void FSceneStateTextToStringFunction::OnExecute(const FSceneStateExecutionContext& InContext, FStructView InFunctionInstance) const
{
	FInstanceDataType& InstanceData = InFunctionInstance.Get<FInstanceDataType>();
	InstanceData.Output = InstanceData.Value.ToString();
}

void FSceneStateNameToStringFunction::OnExecute(const FSceneStateExecutionContext& InContext, FStructView InFunctionInstance) const
{
	FInstanceDataType& InstanceData = InFunctionInstance.Get<FInstanceDataType>();
	InstanceData.Output = InstanceData.Value.ToString();
}

void FSceneStateIntegerToStringFunction::OnExecute(const FSceneStateExecutionContext& InContext, FStructView InFunctionInstance) const
{
	FInstanceDataType& InstanceData = InFunctionInstance.Get<FInstanceDataType>();
	InstanceData.Output = FString::FromInt(InstanceData.Value);
}

void FSceneStateDoubleToStringFunction::OnExecute(const FSceneStateExecutionContext& InContext, FStructView InFunctionInstance) const
{
	FInstanceDataType& InstanceData = InFunctionInstance.Get<FInstanceDataType>();
	InstanceData.Output = FString::SanitizeFloat(InstanceData.Value);
}

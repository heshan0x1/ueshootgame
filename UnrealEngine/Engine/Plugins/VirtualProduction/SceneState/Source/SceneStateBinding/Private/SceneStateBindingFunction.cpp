// Copyright Epic Games, Inc. All Rights Reserved.

#include "SceneStateBindingFunction.h"

#if WITH_EDITOR
FSceneStateBindingFunction::FSceneStateBindingFunction(const UE::SceneState::FBindingFunctionInfo& InFunctionInfo)
{
	FunctionId = FGuid::NewGuid();
	Function = InFunctionInfo.FunctionTemplate;
	FunctionInstance = InFunctionInfo.InstanceTemplate;
}

bool FSceneStateBindingFunction::IsValid() const
{
	return FunctionId.IsValid() && Function.IsValid() && FunctionInstance.IsValid();
}
#endif

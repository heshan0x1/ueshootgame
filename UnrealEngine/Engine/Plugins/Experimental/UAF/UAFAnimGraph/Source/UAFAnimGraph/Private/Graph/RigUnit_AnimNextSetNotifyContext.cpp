// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigUnit_AnimNextSetNotifyContext.h"

#include "Components/SkeletalMeshComponent.h"
#include "Module/AnimNextModuleInstance.h"
#include "Traits/NotifyDispatcher.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RigUnit_AnimNextSetNotifyContext)

FRigUnit_AnimNextSetNotifyContext_Execute()
{
	FAnimNextModuleInstance& ModuleInstance = ExecuteContext.GetContextData<FAnimNextModuleContextData>().GetModuleInstance();
	FAnimNextNotifyDispatcherComponent& NotifyDispatcher = ModuleInstance.GetComponent<FAnimNextNotifyDispatcherComponent>();
	NotifyDispatcher.SkeletalMeshComponent = SkeletalMeshComponent;
	NotifyDispatcher.NotifyQueue.PredictedLODLevel = SkeletalMeshComponent ? SkeletalMeshComponent->GetPredictedLODLevel() : 0;
}

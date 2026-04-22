// Copyright Epic Games, Inc. All Rights Reserved.

#include "Factory/AnimNextFactoryParams.h"
#include "Graph/AnimNextGraphInstance.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AnimNextFactoryParams)

void FAnimNextFactoryParams::InitializeInstance(FUAFAssetInstance& InInstance) const
{
	if (Builder.Stacks.Num() > 0)
	{
		for (const TInstancedStruct<FAnimNextTraitSharedData>& InitializePayload : Builder.Stacks[0].TraitStructs)
		{
			InInstance.AccessVariablesStruct(InitializePayload.GetScriptStruct(), [&InitializePayload](FStructView InStructView)
			{
				check(InitializePayload.GetScriptStruct() == InStructView.GetScriptStruct());
				InStructView.GetScriptStruct()->CopyScriptStruct(InStructView.GetMemory(), InitializePayload.GetMemory());
			});
		}
	}

	const UE::UAF::FInstanceTaskContext TaskContext(InInstance);
	for (const UE::UAF::FInstanceTask& InitializeTask : InitializeTasks)
	{
		InitializeTask(TaskContext);
	}
}

void FAnimNextFactoryParams::Reset()
{
	Builder.Reset();
	InitializeTasks.Empty();
}

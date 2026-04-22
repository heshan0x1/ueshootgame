// Copyright Epic Games, Inc. All Rights Reserved.

#include "ComputeViewExtension.h"

#include "ComputeWorkerInterface.h"

FComputeViewExtension::FComputeViewExtension(const FAutoRegister& AutoReg, UWorld* InWorld)
	: FWorldSceneViewExtension(AutoReg, InWorld)
{
}

void FComputeViewExtension::PostTLASBuild_RenderThread(FRDGBuilder& GraphBuilder, FSceneView& InView)
{
	const TWeakObjectPtr<UWorld> WorldPtr = GetWorld();
	if (!WorldPtr.IsValid())
	{
		return;
	}

	const FSceneInterface* Scene = WorldPtr->Scene;
	if (!Scene)
	{
		return;
	}

	const ERHIFeatureLevel::Type FeatureLevel = Scene->GetFeatureLevel();

	ComputeWorkers.Empty(ComputeWorkers.Num());
	Scene->GetComputeTaskWorkers(ComputeWorkers);

	for (IComputeTaskWorker* ComputeTaskWorker : ComputeWorkers)
	{
		if (ComputeTaskWorker->HasWork(ComputeTaskExecutionGroup::PostTLASBuild))
		{
			FComputeContext Context
			{
				.GraphBuilder = GraphBuilder,
				.ExecutionGroupName = ComputeTaskExecutionGroup::PostTLASBuild,
				.FeatureLevel = FeatureLevel,
				.Scene = Scene,
				.View = &InView,
			};

			ComputeTaskWorker->SubmitWork(Context);
		}
	}
}

ESceneViewExtensionFlags FComputeViewExtension::GetFlags() const
{
	const TWeakObjectPtr<UWorld> WorldPtr = GetWorld();

	if (const FSceneInterface* Scene = WorldPtr.IsValid() ? WorldPtr->Scene : nullptr)
	{
		ComputeWorkers.Empty(ComputeWorkers.Num());
		Scene->GetComputeTaskWorkers(ComputeWorkers);

		for (IComputeTaskWorker* ComputeTaskWorker : ComputeWorkers)
		{
			if (ComputeTaskWorker->HasWork(ComputeTaskExecutionGroup::PostTLASBuild))
			{
				return ESceneViewExtensionFlags::SubscribesToPostTLASBuild;
			}
		}
	}

	return ESceneViewExtensionFlags::None;
}

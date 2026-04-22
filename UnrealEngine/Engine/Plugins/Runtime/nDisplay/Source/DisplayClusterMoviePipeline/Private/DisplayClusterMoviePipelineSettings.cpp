// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterMoviePipelineSettings.h"
#include "DisplayClusterConfigurationTypes.h"
#include "MovieRenderPipelineCoreModule.h"
#include "Modules/ModuleManager.h"

///////////////////////////////////////////////////////////////////////////////////////////
// UDisplayClusterMoviePipelineSettings
///////////////////////////////////////////////////////////////////////////////////////////
ADisplayClusterRootActor* UDisplayClusterMoviePipelineSettings::GetRootActor(const UWorld* InWorld) const
{
	if (InWorld)
	{
		for (const TWeakObjectPtr<ADisplayClusterRootActor> RootActorRef : TActorRange<ADisplayClusterRootActor>(InWorld))
		{
			if (ADisplayClusterRootActor* RootActorPtr = RootActorRef.Get())
			{
				if (!Configuration.DCRootActor.IsValid() || RootActorPtr->GetFName() == Configuration.DCRootActor->GetFName())
				{
					return RootActorPtr;
				}
			}
		}
	}

	return nullptr;
}

bool UDisplayClusterMoviePipelineSettings::GetViewports(const UWorld* InWorld, TArray<FString>& OutViewports, TArray<FIntPoint>& OutViewportResolutions) const
{
	OutViewports.Reset();
	OutViewportResolutions.Reset();

	// Treat an empty list of allowed viewports as "allow all"
	const bool bRenderAllViewports = Configuration.bRenderAllViewports || Configuration.AllowedViewportNamesList.IsEmpty();

	if (ADisplayClusterRootActor* RootActorPtr = GetRootActor(InWorld))
	{
		if (const UDisplayClusterConfigurationData* InConfigurationData = RootActorPtr->GetConfigData())
		{
			if (const UDisplayClusterConfigurationCluster* InClusterCfg =  InConfigurationData->Cluster)
			{
				for (const TPair<FString, TObjectPtr<UDisplayClusterConfigurationClusterNode>>& NodeIt : InClusterCfg->Nodes)
				{
					if (const UDisplayClusterConfigurationClusterNode* InConfigurationClusterNode = NodeIt.Value)
					{
						const FString& InClusterNodeId = NodeIt.Key;
						for (const TPair<FString, TObjectPtr<UDisplayClusterConfigurationViewport>>& InConfigurationViewportIt : InConfigurationClusterNode->Viewports)
						{
							if (const UDisplayClusterConfigurationViewport* InConfigurationViewport = InConfigurationViewportIt.Value)
							{
								if (InConfigurationViewport->bAllowRendering)
								{
									const FString& InViewportId = InConfigurationViewportIt.Key;
									if (bRenderAllViewports || Configuration.AllowedViewportNamesList.Find(InViewportId) != INDEX_NONE)
									{
										OutViewports.Add(InViewportId);

										if (Configuration.bUseViewportResolutions)
										{
											OutViewportResolutions.Add(InConfigurationViewportIt.Value->Region.ToRect().Size());
										}
									}
								}
							}
						}
					}
				}

				return OutViewports.Num() > 0;
			}
		}
	}

	return false;
}

IMPLEMENT_MODULE(FDefaultModuleImpl, DisplayClusterMoviePipeline);

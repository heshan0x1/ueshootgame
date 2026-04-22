// Copyright Epic Games, Inc. All Rights Reserved.

#include "Config/TaggedAssetBrowserConfig.h"

TStrongObjectPtr<UTaggedAssetBrowserConfig> UTaggedAssetBrowserConfig::Instance = nullptr;

UTaggedAssetBrowserConfig* UTaggedAssetBrowserConfig::Get()
{	
	if(!Instance)
	{
		Instance.Reset(NewObject<UTaggedAssetBrowserConfig>());
		Instance->LoadEditorConfig();
	}
	
	return Instance.Get();
}

void UTaggedAssetBrowserConfig::Shutdown()
{
	if(Instance)
	{
		Instance->SaveEditorConfig();
		Instance.Reset();
	}
}

void UTaggedAssetBrowserConfig::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	OnPropertyChangedDelegate.Broadcast(PropertyChangedEvent);
	SaveEditorConfig();
}

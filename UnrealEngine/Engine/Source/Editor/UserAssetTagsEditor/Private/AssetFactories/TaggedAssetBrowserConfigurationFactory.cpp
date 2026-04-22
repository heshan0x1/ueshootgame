// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetFactories/TaggedAssetBrowserConfigurationFactory.h"
#include "Assets/TaggedAssetBrowserConfiguration.h"

UTaggedAssetBrowserConfigurationFactory::UTaggedAssetBrowserConfigurationFactory()
{
	SupportedClass = UTaggedAssetBrowserConfiguration::StaticClass();
	bEditAfterNew = true;
	bCreateNew = true;
}

UObject* UTaggedAssetBrowserConfigurationFactory::FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	UTaggedAssetBrowserConfiguration* NewAsset = NewObject<UTaggedAssetBrowserConfiguration>(InParent, InClass, InName, Flags | RF_Transactional);
	return NewAsset;
}

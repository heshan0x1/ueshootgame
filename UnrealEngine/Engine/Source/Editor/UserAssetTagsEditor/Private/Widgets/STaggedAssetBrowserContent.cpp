// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/STaggedAssetBrowserContent.h"

#include "ContentBrowserModule.h"
#include "IContentBrowserDataModule.h"
#include "IContentBrowserSingleton.h"
#include "SlateOptMacros.h"



#define LOCTEXT_NAMESPACE "TaggedAssetBrowserContent"

void STaggedAssetBrowserContent::Construct(const FArguments& InArgs)
{	
	FAssetPickerConfig Config = InArgs._InitialConfig;
	Config.GetCurrentSelectionDelegates.Add(&GetCurrentSelectionDelegate);
	Config.RefreshAssetViewDelegates.Add(&RefreshAssetViewDelegate);
	Config.SyncToAssetsDelegates.Add(&SyncToAssetsDelegate);
	Config.SetFilterDelegates.Add(&SetNewFilterDelegate);
	Config.bCanShowRealTimeThumbnails = true;
	
	FContentBrowserModule& ContentBrowserModule = FModuleManager::Get().LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
	ChildSlot
	[
		ContentBrowserModule.Get().CreateAssetPicker(Config)
	];
}

void STaggedAssetBrowserContent::SetARFilter(FARFilter InFilter)
{
	SetNewFilterDelegate.Execute(InFilter);
}

TArray<FAssetData> STaggedAssetBrowserContent::GetCurrentSelection() const
{
	return GetCurrentSelectionDelegate.Execute();
}

#undef LOCTEXT_NAMESPACE



// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DataHierarchyViewModelBase.h"
#include "Modules/ModuleManager.h"
#include "Delegates/DelegateCombinations.h"

struct FHierarchyElementViewModel;
class UTaggedAssetBrowserFilterBase;
class SDockTab;
class FSpawnTabArgs;
class FAssetRegistryTagsContext;

USERASSETTAGSEDITOR_API DECLARE_LOG_CATEGORY_EXTERN(LogUserAssetTags, Log, All);

class USERASSETTAGSEDITOR_API FUserAssetTagsEditorModule : public IModuleInterface
{
public:
	DECLARE_DELEGATE_RetVal_ThreeParams(TSharedPtr<FHierarchyElementViewModel>, FOnGetViewModelFactory, UHierarchyElement* Element, TSharedPtr<FHierarchyElementViewModel> InParent, TWeakObjectPtr<UDataHierarchyViewModelBase> InViewModel);

	FUserAssetTagsEditorModule();
	virtual ~FUserAssetTagsEditorModule() override;
	
	static FUserAssetTagsEditorModule& Get();
	
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	/** To register a custom view model for display in the Tagged Asset Browser. */
	void RegisterConfigurationHierarchyElementViewModel(TSubclassOf<UHierarchyElement> FilterType, FOnGetViewModelFactory InFactory);
	void UnregisterConfigurationHierarchyElementViewModel(TSubclassOf<UHierarchyElement> FilterType);
	const TMap<TSubclassOf<UHierarchyElement>, FOnGetViewModelFactory>& GetConfigurationHierarchyViewModelFactories() const { return ConfigurationHierarchyViewModelFactories; }

	static FName ManageTagsTabId;
private:
	void RegisterDefaultConfigurationHierarchyElementViewModels();
	void RegisterDetailsCustomizations();

	/** We extend the Content Browser command list so that the shortcut works. */
	void OnRegisterCommandList(FName ContextName, TSharedRef<FUICommandList> CommandList);

	static TSharedRef<SDockTab> SpawnUserAssetTagsEditorNomadTab(const FSpawnTabArgs& SpawnTabArgs);

	static void GetAssetRegistryTagsFromUserAssetTags(FAssetRegistryTagsContext AssetRegistryTagsContext);
	static void TransferUserAssetTagsFromOldPackageToNewPackage(const FMetaData& OldMetaData, FMetaData& NewMetaData, const UPackage* OldPackage, UPackage* NewPackage);

private:
	TMap<TSubclassOf<UHierarchyElement>, FOnGetViewModelFactory> ConfigurationHierarchyViewModelFactories;
	TSharedPtr<FUICommandList> CommandList;
	FDelegateHandle GetAssetRegistryTagsFromUserAssetTagsDelegateHandle;
	FDelegateHandle TransferUserAssetTagsFromOldPackageToNewPackageDelegateHandle;
};

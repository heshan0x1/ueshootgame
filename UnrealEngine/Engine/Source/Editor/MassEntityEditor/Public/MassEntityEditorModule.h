// Copyright Epic Games, Inc. All Rights Reserved.

#pragma  once

#include "Modules/ModuleInterface.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "Delegates/IDelegateInstance.h"

#define UE_API MASSENTITYEDITOR_API

class IMassEntityEditor;
struct FGraphPanelNodeFactory;
struct FGraphNodeClassHelper;
class UWorld;

/**
* The public interface to this module
*/
class FMassEntityEditorModule : public IModuleInterface, public IHasMenuExtensibility, public IHasToolBarExtensibility
{
public:
	UE_API virtual void StartupModule() override;
	UE_API virtual void ShutdownModule() override;

	virtual TSharedPtr<FExtensibilityManager> GetMenuExtensibilityManager() override
	{
		return MenuExtensibilityManager;
	}

	virtual TSharedPtr<FExtensibilityManager> GetToolBarExtensibilityManager() override
	{
		return ToolBarExtensibilityManager;
	}

	TSharedPtr<FGraphNodeClassHelper> GetProcessorClassCache()
	{
		return ProcessorClassCache;
	}

protected:
#if WITH_UNREAL_DEVELOPER_TOOLS
	static UE_API void OnWorldCleanup(UWorld* /*World*/, bool /*bSessionEnded*/, bool /*bCleanupResources*/);
	FDelegateHandle OnWorldCleanupHandle;
#endif // WITH_UNREAL_DEVELOPER_TOOLS

	TSharedPtr<FGraphNodeClassHelper> ProcessorClassCache;

	TSharedPtr<FExtensibilityManager> MenuExtensibilityManager;
	TSharedPtr<FExtensibilityManager> ToolBarExtensibilityManager;
};

#undef UE_API

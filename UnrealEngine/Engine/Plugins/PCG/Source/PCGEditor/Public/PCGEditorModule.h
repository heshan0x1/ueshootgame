// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Editor/IPCGEditorModule.h"

#include "PCGGraphExecutionStateInterface.h"

#include "Utils/PCGNodeVisualLogs.h"

#include "AssetTypeCategories.h"
#include "Containers/Ticker.h"
#include "Modules/ModuleInterface.h"
#include "Toolkits/AssetEditorToolkit.h"

class ILevelEditor;
// Logs
DECLARE_LOG_CATEGORY_EXTERN(LogPCGEditor, Log, All);

class FMenuBuilder;
class FPCGEditorGraphNodeFactory;
class IAssetTypeActions;

class FPCGEditorModule : public IPCGEditorModule
{
public:
	// ~IModuleInterface implementation
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	virtual bool SupportsDynamicReloading() override;
	// ~End IModuleInterface implementation

	// ~IPCGEditorModule implementation
	virtual TWeakPtr<IPCGEditorProgressNotification> CreateProgressNotification(const FTextFormat& TextFormat, bool bCanCancel) override;
	virtual void ReleaseProgressNotification(TWeakPtr<IPCGEditorProgressNotification> InNotification) override;
	virtual void SetOutlinerUIRefreshDelay(float InDelay) override;
	virtual const FPCGNodeVisualLogs& GetNodeVisualLogs() const override { return NodeVisualLogs; }
	virtual FPCGNodeVisualLogs& GetNodeVisualLogsMutable() override { return NodeVisualLogs; }
	virtual bool CanSelectPartitionActors() const override;

protected:
	virtual void OnScheduleGraph(const FPCGStackContext& StackContext) override;
	virtual void OnGraphPreSave(UPCGGraph* Graph, FObjectPreSaveContext ObjectSaveContext) override;
	virtual void ClearExecutionMetadata(IPCGGraphExecutionSource* InSource) override;
	virtual void ClearExecutedStacks(const IPCGGraphExecutionSource* InRootSource) override;
	virtual void ClearExecutedStacks(const UPCGGraph* InContainingGraph) override;
	virtual	TArray<FPCGStackSharedPtr> GetExecutedStacksPtrs(const FPCGStack& BeginningWithStack) override;
	virtual TArray<FPCGStackSharedPtr> GetExecutedStacksPtrs(const IPCGGraphExecutionSource* InSource, const UPCGGraph* InSubgraph, bool bOnlyWithSubgraphAsCurrentFrame) override;
	virtual void NotifyGraphChanged(UPCGGraph* InGraph, EPCGChangeType ChangeType) override;
	// ~End IPCGEditorModule implementation

	void RegisterDetailsCustomizations();
	void UnregisterDetailsCustomizations();
	void RegisterMenuExtensions();
	void UnregisterMenuExtensions();
	void PopulateMenuActions(FMenuBuilder& MenuBuilder);
	void RegisterSettings();
	void UnregisterSettings();
	void RegisterPCGDataVisualizations();
	void UnregisterPCGDataVisualizations();
	void RegisterPinColorAndIcons();
	void UnregisterPinColorAndIcons();

	void OnLevelEditorCreated(TSharedPtr<ILevelEditor> InLevelEditor);
	void RegisterOnEditorModeChange();
	void OnEditorModeIDChanged(const FEditorModeID& EditorModeID, bool bIsEntering);

	void OnPreExit();

	/** [EXPERIMENTAL] Used to refresh procedural instances when materials are modified which can otherwise be lost.
	* Note: This function subject to change/removal without deprecation.
	*/
	void OnSceneMaterialsModified();

	bool ShouldDisableCPUThrottling();

	TArray<TSharedRef<IAssetTypeActions>> RegisteredAssetTypeActions;
	static EAssetTypeCategories::Type PCGAssetCategory;

	TSharedPtr<FExtensibilityManager> MenuExtensibilityManager;
	TSharedPtr<FExtensibilityManager> ToolBarExtensibilityManager;

	TSharedPtr<FPCGEditorGraphNodeFactory> GraphNodeFactory;

	FDelegateHandle ShouldDisableCPUThrottlingDelegateHandle;

	TSet<TSharedPtr<IPCGEditorProgressNotification>> ActiveNotifications;

	FPCGNodeVisualLogs NodeVisualLogs;

	/** A record of stacks that were executed. Used to populate debugging tool UIs. */
	TSet<FPCGStackSharedPtr> ExecutedStacks;
	TMap<FPCGSoftGraphExecutionSource, TSet<FPCGStackSharedPtr>> ExecutedStacksPerSource;
	mutable FRWLock ExecutedStacksLock;
};

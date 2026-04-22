// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PCGEditor.h"
#include "DataTypes/PVData.h"
#include "SPVEditorViewport.h"

class SCollectionSpreadSheetWidget;
struct FManagedArrayCollection;
class UPCGDefaultExecutionSource;
class UProceduralVegetation;
class UPCGEditorGraphNodeBase;

class FPVEditor : public FPCGEditor
{
public:
	FPVEditor();

	void Initialize(const EToolkitMode::Type InMode, const TSharedPtr<class IToolkitHost>& InToolkitHost, UProceduralVegetation* InProceduralVegetation);
	virtual TSubclassOf<UPCGEditorGraphSchema> GetSchemaClass() const override;

	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	
	// ~Begin FAssetEditorToolkit interface
	virtual FText GetToolkitName() const override;
	virtual FName GetToolkitFName() const override;
	virtual FText GetBaseToolkitName() const override;
	virtual FText GetToolkitToolTipText() const override;
	virtual FLinearColor GetWorldCentricTabColorScale() const override;
	virtual FString GetWorldCentricTabPrefix() const override;
	virtual void OnClose() override;
	virtual void RegisterTabSpawners(const TSharedRef<class FTabManager>& InTabManager) override;
	virtual void UnregisterTabSpawners(const TSharedRef<class FTabManager>& InTabManager) override;
	// ~End FAssetEditorToolkit interface

	virtual IPCGBaseSubsystem* GetSubsystem() const override;

protected:
	// ~Begin FPCGEditor interface
	virtual TAttribute<FGraphAppearanceInfo> GetAppearanceInfo() const override;
	virtual TSharedRef<FTabManager::FLayout> GetDefaultLayout() const override;
	virtual bool IsPanelAvailable(const FName PanelID) const override;
	virtual void RegisterToolbarInternal(FToolMenuSection& PCGSection) const override;
	virtual void BindCommands() override;
	virtual void OnSelectedNodesChanged(const TSet<UObject*>& InNewSelection) override;
	// ~End FPCGEditor interface

	bool bGenerationInProgress = false;
	TObjectPtr<UPCGDefaultExecutionSource> ExecutionSource;

private:
	static const FName CollectionSpreadSheetTabId;
	
	TObjectPtr<UProceduralVegetation> ProceduralVegetationBeingEdited = nullptr;

	TArray<TObjectPtr<UPCGEditorGraphNodeBase>> SelectedNodes;
	TSharedPtr<FManagedArrayCollection> CollectionBeingInspected = nullptr;
	TSharedPtr<FManagedArrayCollection> SelectedCollection = nullptr;
	TObjectPtr<UPCGEditorGraphNodeBase> NodeBeingInspected = nullptr;
	
	TArray<TSharedRef<SPVEditorViewport>> ViewportWidgets;
	TSharedPtr<SCollectionSpreadSheetWidget> CollectionSpreadSheetWidget;

	TWeakPtr<SDockTab> CollectionSpreadSheetTab;
	
	FDateTime SessionStartTime;
	
	TSharedRef<SCollectionSpreadSheetWidget> CreateCollectionSpreadSheetWidget();

	TSharedRef<SDockTab> SpawnTab_CollectionSpreadSheet(const FSpawnTabArgs& SpawnTabArgs);
	
	void UpdateCollectionSpreadSheet(const UPCGNode* InSelectedNode);

	void GatherStats(TArray<FText>& OverlayStats);
	
	void OnExport_Clicked();

	FReply ExportInternal();
	
	void OnLockNodeSelection();

	void ChangeNodeInspection(UPCGEditorGraphNodeBase* InNode);

protected:
	virtual TSharedRef<SPCGEditorViewport> CreateViewportWidget() override;
	
	const UPVData* GetPVDataFromNode(const UPCGNode* InNode, FName& OutPinName) const;
	virtual bool CanToggleInspected() const override;
	bool GetCollectionFromNode(const UPCGNode* InNode, FName& OutPinName, FManagedArrayCollection& OutCollection) const;

	UPCGEditorGraphNodeBase* GetSelectedNode();
};

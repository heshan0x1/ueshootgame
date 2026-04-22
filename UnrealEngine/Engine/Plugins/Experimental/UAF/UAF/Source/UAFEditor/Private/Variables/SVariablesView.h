// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ContentBrowserDelegates.h"
#include "SSceneOutliner.h"
#include "WorkspaceAssetRegistryInfo.h"
#include "Editor/Persona/Private/SAnimAssetFindReplace.h"
#include "Widgets/SCompoundWidget.h"
#include "WorkflowOrientedApp/WorkflowTabFactory.h"

enum class EAnimNextEditorDataNotifType : uint8;
class UAnimNextRigVMAsset;
struct FWorkspaceOutlinerItemExport;
class SPositiveActionButton;
class UAnimNextRigVMAssetEditorData;

namespace UE::Workspace
{
	class IWorkspaceEditor;
	struct FWorkspaceDocument;
}

namespace UE::UAF::Editor
{
	class SVariablesOutliner;
}

namespace UE::UAF::Editor
{

extern const FLazyName VariablesTabName;

class SVariablesOutliner : public SSceneOutliner
{
public:
	void SetExport(const FWorkspaceOutlinerItemExport& InExport);
	void OnEditorDataModified(UAnimNextRigVMAssetEditorData* InEditorData, EAnimNextEditorDataNotifType InType, UObject* InSubject);

	void SetHighlightedItem(FSceneOutlinerTreeItemPtr Item) const;
	void ClearHighlightedItem(FSceneOutlinerTreeItemPtr Item) const; 
	
	bool HasAssets() const;
protected:
	
	void SetAssets(TConstArrayView<TSoftObjectPtr<UAnimNextRigVMAsset>> InAssets);
	void UpdateAssets();

	void HandleAssetLoaded(const FSoftObjectPath& InSoftObjectPath, UAnimNextRigVMAsset* InAsset);
	
	void RegisterAssetDelegates(const UAnimNextRigVMAsset* InAsset);
	void UnregisterAssetDelegates(const UAnimNextRigVMAsset* InAsset);
private:
	friend class FVariablesOutlinerMode;
	friend class FVariablesOutlinerHierarchy;

	TArray<TSoftObjectPtr<UAnimNextRigVMAsset>> Assets;
	FWorkspaceOutlinerItemExport Export;
};

class SVariablesView : public SCompoundWidget
{
	SLATE_BEGIN_ARGS(SVariablesView) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, TSharedRef<UE::Workspace::IWorkspaceEditor> InWorkspaceEditor);

	void SetExportDirectly(const FWorkspaceOutlinerItemExport& InExport) const;
private:
	void HandleFocusedDocumentChanged(const UE::Workspace::FWorkspaceDocument& InDocument) const;

private:
	friend struct FAnimNextVariablesTabSummoner;
	TSharedPtr<SVariablesOutliner> VariablesOutliner;
};

struct FAnimNextVariablesTabSummoner : public FWorkflowTabFactory
{
public:
	FAnimNextVariablesTabSummoner(TSharedPtr<UE::Workspace::IWorkspaceEditor> InHostingApp);

private:
	// FWorkflowTabFactory interface
	virtual TSharedRef<SWidget> CreateTabBody(const FWorkflowTabSpawnInfo& Info) const override;
	virtual FText GetTabToolTipText(const FWorkflowTabSpawnInfo& Info) const override;

	// The widget this tab spawner wraps
	TSharedPtr<SVariablesView> VariablesView;
};


};
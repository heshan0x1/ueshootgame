// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AnimNextRigVMAssetEditorData.h"
#include "ISceneOutlinerMode.h"
#include "Common/AnimNextAssetFindReplaceVariables.h"

class UAnimNextRigVMAssetEditorData;
class FVariablesOutlinerMode;
namespace UE::Workspace
{
	class IWorkspaceEditor;
}

namespace UE::UAF::Editor
{
	class SVariablesOutliner;
}

namespace UE::UAF::Editor
{
class FVariablesOutlinerMode : public ISceneOutlinerMode
{
public:
	FVariablesOutlinerMode(SVariablesOutliner* InVariablesOutliner, const TSharedRef<UE::Workspace::IWorkspaceEditor>& InWorkspaceEditor);

	// Begin ISceneOutlinerMode overrides
	virtual void Rebuild() override;
	virtual TSharedPtr<SWidget> CreateContextMenu() override;
	virtual FReply OnKeyDown(const FKeyEvent& InKeyEvent) override;
	virtual void OnItemClicked(FSceneOutlinerTreeItemPtr Item) override;
	void HandleItemSelection(const FSceneOutlinerItemSelection& Selection) const;
	virtual void OnItemSelectionChanged(FSceneOutlinerTreeItemPtr Item, ESelectInfo::Type SelectionType, const FSceneOutlinerItemSelection& Selection) override;
	virtual bool CanCustomizeToolbar() const override { return true; }
	virtual bool CanRenameItem(const ISceneOutlinerTreeItem& Item) const override;
	virtual ESelectionMode::Type GetSelectionMode() const override { return ESelectionMode::Multi; }
	virtual void BindCommands(const TSharedRef<FUICommandList>& OutCommandList) override;
	virtual int32 GetTypeSortPriority(const ISceneOutlinerTreeItem& Item) const override;
	virtual TSharedPtr<FDragDropOperation> CreateDragDropOperation(const FPointerEvent& MouseEvent, const TArray<FSceneOutlinerTreeItemPtr>& InTreeItems) const override;
	virtual bool ParseDragDrop(FSceneOutlinerDragDropPayload& OutPayload, const FDragDropOperation& Operation) const override;
	virtual FSceneOutlinerDragValidationInfo ValidateDrop(const ISceneOutlinerTreeItem& DropTarget, const FSceneOutlinerDragDropPayload& Payload) const override;
	virtual void OnDrop(ISceneOutlinerTreeItem& DropTarget, const FSceneOutlinerDragDropPayload& Payload, const FSceneOutlinerDragValidationInfo& ValidationInfo) const override;
	virtual FReply OnDragOverItem(const FDragDropEvent& Event, const ISceneOutlinerTreeItem& Item) const override;
	virtual bool CanSupportDragAndDrop() const override { return true; }
	virtual void OnItemDoubleClick(FSceneOutlinerTreeItemPtr Item) override;
	
	void SetHighlightedItem(FSceneOutlinerTreeItemID InID) const;
	void ClearHighlightedItem(FSceneOutlinerTreeItemID InID) const;

	static void PopulateNewVariableToolMenuEntries(UToolMenu* InMenu, bool bAddSeparator);
protected:
	virtual TUniquePtr<ISceneOutlinerHierarchy> CreateHierarchy() override;
	// End ISceneOutlinerMode overrides

	void ResetOutlinerSelection() const;

	SVariablesOutliner* GetOutliner() const;

	void Rename() const;
	virtual bool CanRename() const override;

	void Delete() const;
	virtual bool CanDelete() const override;

	void Copy() const;
	virtual bool CanCopy() const override;

	void Paste() const;
	virtual bool CanPaste() const override;

	void ToggleVariableExport() const;
	bool CanToggleVariableExport() const;

	void Duplicate() const;
	bool CanDuplicate() const;

	void FindReferences(ESearchScope InSearchScope) const;
	bool CanFindReferences() const;
	bool IsFindReferencesVisible(ESearchScope InSearchScope) const;

	void SaveAsset() const;
	bool CanSaveAsset() const;
	bool IsAsset() const;

	void CreateSharedVariablesAssets() const;
	bool CanCreateSharedVariablesAssets() const;
private:
	friend class FVariablesOutlinerHierarchy;

	TWeakPtr<UE::Workspace::IWorkspaceEditor> WeakWorkspaceEditor;

	TSharedPtr<FUICommandList> CommandList;
};

}

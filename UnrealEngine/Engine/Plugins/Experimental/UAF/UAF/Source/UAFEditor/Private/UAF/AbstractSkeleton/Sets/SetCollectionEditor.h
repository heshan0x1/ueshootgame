// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EditorUndoClient.h"
#include "UAF/AbstractSkeleton/AbstractSkeletonSetCollection.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/STreeView.h"

namespace UE::UAF
{
	class FSetCollectionEditorToolkit : public FAssetEditorToolkit
	{
	public:
		void InitEditor(const TArray<UObject*>& InObjects);

		// Begin FAssetEditorToolkit
		void RegisterTabSpawners(const TSharedRef<class FTabManager>& TabManager) override;
		void UnregisterTabSpawners(const TSharedRef<class FTabManager>& TabManager) override;

		FName GetToolkitFName() const override { return "AbstractSkeletonSetCollectionEditor"; }
		FText GetBaseToolkitName() const override { return INVTEXT("Abstract Skeleton Set Collection Editor"); }
		FString GetWorldCentricTabPrefix() const override { return "Abstract Skeleton Set Collection Editor "; }
		FLinearColor GetWorldCentricTabColorScale() const override { return {}; }
		// End FAssetEditorToolkit

	private:
		TSharedRef<SDockTab> SpawnPropertiesTab(const FSpawnTabArgs& Args);

		TWeakObjectPtr<UAbstractSkeletonSetCollection> SetCollection;
	};

	class SSetCollection : public SCompoundWidget, public FSelfRegisteringEditorUndoClient
	{
		SLATE_BEGIN_ARGS(SSetCollection) {}
		SLATE_END_ARGS()

	public:
		struct FTreeItem
		{
			FName Name;
			FName ParentName;
			TArray<TSharedPtr<FTreeItem>> Children;

			DECLARE_DELEGATE(FOnRequestRename);
			FOnRequestRename OnRequestRename;

			DECLARE_DELEGATE_RetVal_TwoParams(bool, FCanRenameTo, const FName /* Old Name */, const FName /* New Name */);
			FCanRenameTo CanRenameTo;

			DECLARE_DELEGATE_TwoParams(FOnRenamed, const FName /* Old Name */, const FName /* New Name */);
			FOnRenamed OnRenamed;
		};

		void Construct(const FArguments& InArgs, TWeakObjectPtr<UAbstractSkeletonSetCollection> InSetCollection);

		/** FSelfRegisteringEditorUndoClient interface */
		virtual void PostUndo(bool bSuccess) override;
		virtual void PostRedo(bool bSuccess) override;

	private:
		virtual FReply OnKeyDown( const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

		TSharedRef<ITableRow> TreeView_OnGenerateRow(TSharedPtr<FTreeItem> InItem, const TSharedRef<STableViewBase>& OwnerTable);

		void TreeView_OnGetChildren(TSharedPtr<FTreeItem> InItem, TArray<TSharedPtr<FTreeItem>>& OutChildren);

		TSharedPtr<SWidget> TreeView_OnContextMenuOpening();

		void TreeView_OnItemScrolledIntoView(TSharedPtr<FTreeItem> InItem, const TSharedPtr<ITableRow>& InWidget);
		
		void TreeView_OnMouseButtonDoubleClick(TSharedPtr<FTreeItem> InItem);

		void OnRenameSet();
		
		void OnRemoveSet();

		void RepopulateTreeData();

		void ExpandAllTreeItems();

		void BindCommands();

		TArray<TSharedPtr<FTreeItem>> GetAllTreeItems();

		FReply OnAddSet();

		void OnItemRenamed(const FName OldSetName, const FName NewSetName);

		bool CanRenameItem(const FName OldSetName, const FName NewSetName) const;

	private:
		TSharedPtr<FTreeItem> GetTreeItem(const FName SetName);

		TWeakObjectPtr<UAbstractSkeletonSetCollection> SetCollection;

		TSharedPtr<STreeView<TSharedPtr<FTreeItem>>> TreeView;

		TArray<TSharedPtr<FTreeItem>> RootItems;

		TSharedPtr<SSearchBox> SearchBox;

		TSharedPtr<FUICommandList> CommandList;

		TSharedPtr<FTreeItem> ItemToRename;
	};
}

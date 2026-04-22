// Copyright Epic Games, Inc. All Rights Reserved.

#include "UAF/AbstractSkeleton/Sets/SetCollectionEditor.h"

#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "SPositiveActionButton.h"
#include "Widgets/Input/SSearchBox.h"
#include "Framework/Commands/GenericCommands.h"
#include "DragAndDrop/DecoratedDragDropOp.h"
#include "ScopedTransaction.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"

#define LOCTEXT_NAMESPACE "UE::UAF::FSetCollectionEditorToolkit"

namespace UE::UAF
{
	void FSetCollectionEditorToolkit::InitEditor(const TArray<UObject*>& InObjects)
	{
		SetCollection = CastChecked<UAbstractSkeletonSetCollection>(InObjects[0]);

		const TSharedRef<FTabManager::FLayout> Layout = FTabManager::NewLayout("AbstractSkeletonSetCollectionEditorToolkit")
			->AddArea
			(
				FTabManager::NewPrimaryArea()->SetOrientation(Orient_Horizontal)
				->Split
				(
					FTabManager::NewStack()
					->SetSizeCoefficient(0.7f)
					->AddTab("AbstractSkeletonSetCollection_SetTab", ETabState::OpenedTab)
				)
				->Split
				(
					FTabManager::NewStack()
					->SetSizeCoefficient(0.3f)
					->AddTab("AbstractSkeletonSetCollection_PropertiesTab", ETabState::OpenedTab)
				)
			);

		FAssetEditorToolkit::InitAssetEditor(EToolkitMode::Standalone, {}, "AbstractSkeletonSetCollectionEditorToolkit", Layout, true, true, InObjects);
	}

	void FSetCollectionEditorToolkit::RegisterTabSpawners(const TSharedRef<class FTabManager>& InTabManager)
	{
		FAssetEditorToolkit::RegisterTabSpawners(InTabManager);

		WorkspaceMenuCategory = InTabManager->AddLocalWorkspaceMenuCategory(LOCTEXT("AbstractSkeletonSetCollectionEditor", "Abstract Skeleton Set Editor"));

		// Set tab
		{
			InTabManager->RegisterTabSpawner("AbstractSkeletonSetCollection_SetTab", FOnSpawnTab::CreateLambda([this](const FSpawnTabArgs&)
				{
					return SNew(SDockTab)
						[
							SNew(SSetCollection, SetCollection)
						];
				}))
				.SetDisplayName(LOCTEXT("SetTab_DisplayName", "Abstract Skeleton Set"))
				.SetGroup(WorkspaceMenuCategory.ToSharedRef());
		}

		// Property tab
		{
			InTabManager->RegisterTabSpawner("AbstractSkeletonSetCollection_PropertiesTab", FOnSpawnTab::CreateSP(this, &FSetCollectionEditorToolkit::SpawnPropertiesTab))
				.SetDisplayName(LOCTEXT("PropertiesTab_DisplayName", "Details"))
				.SetGroup(WorkspaceMenuCategory.ToSharedRef());
		}
	}

	void FSetCollectionEditorToolkit::UnregisterTabSpawners(const TSharedRef<class FTabManager>& InTabManager)
	{
		FAssetEditorToolkit::UnregisterTabSpawners(InTabManager);
		InTabManager->UnregisterTabSpawner("AbstractSkeletonSetCollection_SetTab");
		InTabManager->UnregisterTabSpawner("AbstractSkeletonSetCollection_PropertiesTab");
	}

	TSharedRef<SDockTab> FSetCollectionEditorToolkit::SpawnPropertiesTab(const FSpawnTabArgs& Args)
	{
		FPropertyEditorModule& PropertyEditorModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");

		FDetailsViewArgs DetailsViewArgs;
		DetailsViewArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
		DetailsViewArgs.bAllowSearch = false;
		DetailsViewArgs.bUpdatesFromSelection = false;
		DetailsViewArgs.bShowOptions = false;
		DetailsViewArgs.bShowObjectLabel = false;
		DetailsViewArgs.bAllowFavoriteSystem = false;

		TSharedRef<IDetailsView> DetailsView = PropertyEditorModule.CreateDetailView(DetailsViewArgs);
		DetailsView->SetObjects(TArray<UObject*>{ SetCollection.Get() });

		return SNew(SDockTab)
			[
				DetailsView
			];
	}

	class FSetCollectionDragDropOp : public FDecoratedDragDropOp
	{
	public:
		DRAG_DROP_OPERATOR_TYPE(FSetCollectionDragDropOp, FDecoratedDragDropOp)

		static TSharedRef<FSetCollectionDragDropOp> New(const FName InSetName, const FName InParentSetName)
		{
			TSharedRef<FSetCollectionDragDropOp> Operation = MakeShared<FSetCollectionDragDropOp>();
			Operation->SetName = InSetName;
			Operation->ParentSetName = InParentSetName;
			Operation->Construct();
			return Operation;
		}

		TSharedPtr<SWidget> GetDefaultDecorator() const override
		{
			return SNew(SBorder)
				.Visibility(EVisibility::Visible)
				.BorderImage(FAppStyle::GetBrush("Menu.Background"))
				[
					SNew(STextBlock)
						.Text(FText::FromName(SetName))
				];
		}

		FName SetName;
		FName ParentSetName;
	};

	void SSetCollection::Construct(const FArguments& InArgs, TWeakObjectPtr<UAbstractSkeletonSetCollection> InSetCollection)
	{
		SetCollection = InSetCollection;

		ChildSlot
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(FMargin(1.0f, 1.0f))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(FMargin(1.0f, 1.0f))
				[
					SNew(SPositiveActionButton)
					.OnClicked(this, &SSetCollection::OnAddSet)
					.Icon(FAppStyle::Get().GetBrush("Icons.Plus"))
					.Text(LOCTEXT("AddButton_Text", "Add Set"))
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.Padding(FMargin(1.0f, 1.0f))
				[
					SAssignNew(SearchBox, SSearchBox)
					.SelectAllTextWhenFocused(true)
					.OnTextChanged_Lambda([](const FText& InText) { /* TODO: Implement */ })
					.HintText(LOCTEXT("SearchBox_Hint", "Search Sets..."))
				]
			]
			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			.Padding(FMargin(1.0f, 1.0f))
			[
				SAssignNew(TreeView, STreeView<TSharedPtr<FTreeItem>>)
				.SelectionMode(ESelectionMode::Single)
				.HighlightParentNodesForSelection(true)
				.TreeItemsSource(&RootItems)
				.OnGenerateRow(this, &SSetCollection::TreeView_OnGenerateRow)
				.OnGetChildren(this, &SSetCollection::TreeView_OnGetChildren)
				.OnContextMenuOpening(this, &SSetCollection::TreeView_OnContextMenuOpening)
				.OnItemScrolledIntoView(this, &SSetCollection::TreeView_OnItemScrolledIntoView)
				.OnMouseButtonDoubleClick(this, &SSetCollection::TreeView_OnMouseButtonDoubleClick)
				.HeaderRow
				(
					SNew(SHeaderRow)
					+ SHeaderRow::Column(FName("SetHierarchy"))
					.FillWidth(0.5f)
					.DefaultLabel(LOCTEXT("TreeView_Header", "Set Hierarchy"))
				)
			]
		];

		RepopulateTreeData();
		ExpandAllTreeItems();
		BindCommands();
	}

	FReply SSetCollection::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
	{
		if (CommandList->ProcessCommandBindings(InKeyEvent))
		{
			return FReply::Handled();
		}

		return FReply::Unhandled();
	}

	TSharedRef<ITableRow> SSetCollection::TreeView_OnGenerateRow(TSharedPtr<FTreeItem> InItem, const TSharedRef<STableViewBase>& OwnerTable)
	{
		TSharedPtr<SInlineEditableTextBlock> InlineWidget;

		TSharedRef<STableRow<TSharedPtr<FTreeItem>>> RowWidget = SNew(STableRow<TSharedPtr<FTreeItem>>, OwnerTable)
			.Style(&FAppStyle().GetWidgetStyle<FTableRowStyle>("TableView.AlternatingRow"))
			.OnDragDetected_Lambda([InItem](const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
				{
					if (MouseEvent.IsMouseButtonDown(EKeys::LeftMouseButton))
					{
						const TSharedRef<FSetCollectionDragDropOp> DragDropOp = FSetCollectionDragDropOp::New(InItem->Name, InItem->ParentName);
						return FReply::Handled().BeginDragDrop(DragDropOp);
					}

					return FReply::Unhandled();
				})
			.OnCanAcceptDrop_Lambda([](const FDragDropEvent& DragDropEvent, EItemDropZone DropZone, TSharedPtr<FTreeItem> TargetTreeItem) -> TOptional<EItemDropZone>
				{
					using namespace UE::UAF;

					TOptional<EItemDropZone> ReturnedDropZone;

					const TSharedPtr<FSetCollectionDragDropOp> DragDropOp = DragDropEvent.GetOperationAs<FSetCollectionDragDropOp>();
					if (DragDropOp.IsValid())
					{
						ReturnedDropZone = EItemDropZone::OntoItem;
					}

					return ReturnedDropZone;
				})
			.OnAcceptDrop_Lambda([this](const FDragDropEvent& DragDropEvent, EItemDropZone DropZone, TSharedPtr<FTreeItem> TargetTreeItem) -> FReply
				{
					const TSharedPtr<FSetCollectionDragDropOp> DragDropOp = DragDropEvent.GetOperationAs<FSetCollectionDragDropOp>();
					if (DragDropOp.IsValid())
					{
						const FScopedTransaction Transaction(LOCTEXT("ReparentSet", "Reparent Set"));
						SetCollection->Modify();

						SetCollection->ReparentSet(DragDropOp->SetName, TargetTreeItem->Name);
						RepopulateTreeData();

						return FReply::Handled();
					}

					return FReply::Unhandled();
				})
			.ShowWires(true)
			[
				SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(3.0f)
					[
						SNew(SImage)
							.Image(FAppStyle::Get().GetBrush("ClassIcon.GroupActor"))
					]
					+ SHorizontalBox::Slot()
					.FillWidth(1.0)
					.AutoWidth()
					.Padding(3.0f)
					[
						SAssignNew(InlineWidget, SInlineEditableTextBlock)
							.Text(FText::FromName(InItem->Name))
							.OnVerifyTextChanged_Lambda([InItem](const FText& InText, FText& OutErrorMessage)
							{
								const FName OldName = InItem->Name;
								const FName NewName = FName(InText.ToString());

								if (InItem->Name == NewName)
								{
									return true;
								}

								check(InItem->CanRenameTo.IsBound());
								if (InItem->CanRenameTo.Execute(OldName, NewName))
								{
									return true;
								}
								else
								{
									OutErrorMessage = LOCTEXT("SetNameTaken", "Name already taken");
									return false;
								}
							})
							.OnTextCommitted_Lambda([InItem](const FText& InText, ETextCommit::Type CommitInfo)
							{
								const FName OldName = InItem->Name;
								const FName NewName = FName(InText.ToString());

								InItem->OnRenamed.ExecuteIfBound(OldName, NewName);
							})
					]

			];

		InItem->OnRequestRename.BindSP(InlineWidget.Get(), &SInlineEditableTextBlock::EnterEditingMode);

		return RowWidget;
	}

	void SSetCollection::TreeView_OnGetChildren(TSharedPtr<FTreeItem> InItem, TArray<TSharedPtr<FTreeItem>>& OutChildren)
	{
		OutChildren = InItem->Children;
	}

	TSharedPtr<SWidget> SSetCollection::TreeView_OnContextMenuOpening()
	{
		FMenuBuilder MenuBuilder(true, CommandList.ToSharedRef());

		MenuBuilder.AddMenuEntry(FGenericCommands::Get().Rename);
		MenuBuilder.AddMenuEntry(FGenericCommands::Get().Delete);

		return MenuBuilder.MakeWidget();
	}

	void SSetCollection::TreeView_OnItemScrolledIntoView(TSharedPtr<FTreeItem> InItem, const TSharedPtr<ITableRow>& InWidget)
	{
		if (ItemToRename)
		{
			ItemToRename->OnRequestRename.ExecuteIfBound();
			ItemToRename = nullptr;
		}
	}

	void SSetCollection::TreeView_OnMouseButtonDoubleClick(TSharedPtr<FTreeItem> InItem)
	{
		InItem->OnRequestRename.ExecuteIfBound();
	}

	void SSetCollection::PostUndo(bool bSuccess)
	{
		RepopulateTreeData();
	}

	void SSetCollection::PostRedo(bool bSuccess)
	{
		RepopulateTreeData();
	}

	void SSetCollection::OnRemoveSet()
	{
		const TArray<TSharedPtr<FTreeItem>> SelectedItems = TreeView->GetSelectedItems();
		if (!SelectedItems.IsEmpty())
		{
			const FScopedTransaction Transaction(LOCTEXT("RemoveSet", "Remove Set"));
			SetCollection->Modify();

			const FName SetToRemove = SelectedItems[0]->Name;
			const bool bSuccess = SetCollection->RemoveSet(SetToRemove);
			if (bSuccess)
			{
				RepopulateTreeData();
			}
		}
	}

	void SSetCollection::RepopulateTreeData()
	{
		// Make note of the currently selected item, if any
		TArray<TSharedPtr<FTreeItem>> Selected = TreeView->GetSelectedItems();

		// Make note of all tree items expanded
		TArray<FName> ExpandedSetNames;

		for (TSharedPtr<FTreeItem> TreeItem : GetAllTreeItems())
		{
			if (TreeView->IsItemExpanded(TreeItem))
			{
				ExpandedSetNames.Add(TreeItem->Name);
			}
		}

		// Rebuild tree data
		RootItems.Reset();

		TMap<FName, TSharedPtr<FTreeItem>> TreeItemMap;

		for (const FAbstractSkeletonSet& Set : SetCollection->GetSetHierarchy())
		{
			TSharedPtr<FTreeItem> SetItem = MakeShared<FTreeItem>();
			SetItem->Name = Set.SetName;
			SetItem->ParentName = Set.ParentSetName;
			SetItem->OnRenamed = FTreeItem::FOnRenamed::CreateSP(this, &SSetCollection::OnItemRenamed);
			SetItem->CanRenameTo = FTreeItem::FCanRenameTo::CreateSP(this, &SSetCollection::CanRenameItem);

			if (Set.ParentSetName == NAME_None)
			{
				RootItems.Add(SetItem);
			}
			else
			{
				if (!TreeItemMap.Contains(Set.ParentSetName))
				{
					TSharedPtr<FTreeItem> ParentItem = MakeShared<FTreeItem>();
					ParentItem->Name = Set.ParentSetName;
					ParentItem->OnRenamed = FTreeItem::FOnRenamed::CreateSP(this, &SSetCollection::OnItemRenamed);
					ParentItem->CanRenameTo = FTreeItem::FCanRenameTo::CreateSP(this, &SSetCollection::CanRenameItem);

					TreeItemMap.Add(Set.ParentSetName, ParentItem);
				}

				TSharedPtr<FTreeItem> ParentItem = TreeItemMap[Set.ParentSetName];
				ParentItem->Children.Add(SetItem);

			}

			TreeItemMap.Add(Set.SetName, SetItem);
		}

		// Restore expanded state of tree items and selection
		const FName SelectedSetName = Selected.IsEmpty() ? NAME_None : Selected[0]->Name;
		for (TSharedPtr<FTreeItem> TreeItem : GetAllTreeItems())
		{
			if (ExpandedSetNames.Contains(TreeItem->Name))
			{
				TreeView->SetItemExpansion(TreeItem, true);
			}

			if (TreeItem->Name == SelectedSetName)
			{
				TreeView->SetSelection(TreeItem);
			}
		}

		TreeView->RequestTreeRefresh();
	}

	void SSetCollection::ExpandAllTreeItems()
	{
		for (TSharedPtr<FTreeItem> TreeItem : GetAllTreeItems())
		{
			TreeView->SetItemExpansion(TreeItem, true);
		}
	}

	void SSetCollection::BindCommands()
	{
		CommandList = MakeShareable(new FUICommandList);

		CommandList->MapAction(
			FGenericCommands::Get().Rename,
			FExecuteAction::CreateSP(this, &SSetCollection::OnRenameSet)
		);
	
		CommandList->MapAction(
			FGenericCommands::Get().Delete,
			FExecuteAction::CreateSP(this, &SSetCollection::OnRemoveSet)
		);
	}

	TArray<TSharedPtr<SSetCollection::FTreeItem>> SSetCollection::GetAllTreeItems()
	{
		TArray<TSharedPtr<FTreeItem>> AllItems;
		AllItems.Append(RootItems);

		for (int32 Index = 0; Index < AllItems.Num(); ++Index)
		{
			AllItems.Append(AllItems[Index]->Children);
		}

		return AllItems;
	}

	FReply SSetCollection::OnAddSet()
	{
		const FScopedTransaction Transaction(LOCTEXT("AddSet", "Set Set"));
		SetCollection->Modify();

		TArray<TSharedPtr<FTreeItem>> SelectedItems = TreeView->GetSelectedItems();

		const FName ParentSetName = SelectedItems.IsEmpty() ? NAME_None : SelectedItems[0]->Name;
		
		const FText DefaultSetNameFormat = LOCTEXT("DefaultSetName", "NewSet_{0}");

		int32 Suffix = 0;
		FName SetName = FName(FText::Format(DefaultSetNameFormat, Suffix).ToString());
		while (SetCollection->HasSet(SetName))
		{
			++Suffix;
			SetName = FName(FText::Format(DefaultSetNameFormat, Suffix).ToString());
		}

		SetCollection->AddSet(SetName, ParentSetName);
		RepopulateTreeData();

		// Expand the parent set tree item so we can see the new set in the tree
		TreeView->SetItemExpansion(GetTreeItem(ParentSetName), true);

		TSharedPtr<FTreeItem> SetTreeItem = GetTreeItem(SetName);
		ItemToRename = SetTreeItem;
		TreeView->RequestScrollIntoView(SetTreeItem);

		return FReply::Handled();
	}

	void SSetCollection::OnItemRenamed(const FName OldSetName, const FName NewSetName)
	{
		SetCollection->RenameSet(OldSetName, NewSetName);
		RepopulateTreeData();
	}

	bool SSetCollection::CanRenameItem(const FName OldSetName, const FName NewSetName) const
	{
		return !SetCollection->HasSet(NewSetName);
	}

	TSharedPtr<SSetCollection::FTreeItem> SSetCollection::GetTreeItem(const FName SetName)
	{
		for (TSharedPtr<FTreeItem> TreeItem : GetAllTreeItems())
		{
			if (TreeItem->Name == SetName)
			{
				return TreeItem;
			}
		}

		return nullptr;
	}
	
	void SSetCollection::OnRenameSet()
	{
		TArray<TSharedPtr<FTreeItem>> Selected = TreeView->GetSelectedItems();
		if (!Selected.IsEmpty())
		{		
			ItemToRename = Selected[0];
        	TreeView->RequestScrollIntoView(ItemToRename);
        }
	}
	
}

#undef LOCTEXT_NAMESPACE
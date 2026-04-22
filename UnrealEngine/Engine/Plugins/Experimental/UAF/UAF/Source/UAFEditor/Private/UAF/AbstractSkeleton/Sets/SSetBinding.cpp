// Copyright Epic Games, Inc. All Rights Reserved.

#include "UAF/AbstractSkeleton/Sets/SSetBinding.h"

#include "AttributeDragDrop.h"
#include "Framework/Commands/UICommandList.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "ScopedTransaction.h"
#include "SetDragDrop.h"
#include "SkeletonTreeDragDrop.h"
#include "UAF/AbstractSkeleton/AbstractSkeletonSetBinding.h"
#include "UAF/AbstractSkeleton/AbstractSkeletonSetCollection.h"
#include "Widgets/Input/SSearchBox.h"

#define LOCTEXT_NAMESPACE "UE::UAF::Sets::SSetBinding"

namespace UE::UAF::Sets
{
void SSetBinding::Construct(const FArguments& InArgs, TWeakObjectPtr<UAbstractSkeletonSetBinding> InSetBinding)
	{
		SetBinding = InSetBinding;
		OnTreeRefreshed = InArgs._OnTreeRefreshed;

		ChildSlot
			[
				SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(FMargin(1.0f, 1.0f))
					[
						SNew(SHorizontalBox)
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
						SAssignNew(TreeView, STreeView<TSharedPtr<ITreeItem>>)
							.SelectionMode(ESelectionMode::Single)
							.HighlightParentNodesForSelection(true)
							.TreeItemsSource(&RootItems)
							.OnGenerateRow(this, &SSetBinding::TreeView_OnGenerateRow)
							.OnGetChildren(this, &SSetBinding::TreeView_OnGetChildren)
							.OnContextMenuOpening(this, &SSetBinding::TreeView_OnContextMenuOpening)
							.HeaderRow
							(
								SNew(SHeaderRow)
								+ SHeaderRow::Column(FName("SetHierarchy"))
								.FillWidth(0.5f)
								.DefaultLabel(LOCTEXT("TreeView_Header", "Set Bindings"))
							)
					]
			];

		RepopulateTreeData();
		ExpandAllTreeItems();
		BindCommands();
	}

	FReply SSetBinding::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
	{
		if (CommandList->ProcessCommandBindings(InKeyEvent))
		{
			return FReply::Handled();
		}

		return FReply::Unhandled();
	}

	TSharedRef<ITableRow> SSetBinding::TreeView_OnGenerateRow(TSharedPtr<ITreeItem> InItem, const TSharedRef<STableViewBase>& OwnerTable)
	{
		return InItem->GenerateRow(OwnerTable);
	}

	void SSetBinding::TreeView_OnGetChildren(TSharedPtr<ITreeItem> InItem, TArray<TSharedPtr<ITreeItem>>& OutChildren)
	{
		InItem->GetChildren(OutChildren);
	}

	TSharedPtr<SWidget> SSetBinding::TreeView_OnContextMenuOpening()
	{
		FMenuBuilder MenuBuilder(true, CommandList.ToSharedRef());

		TArray<TSharedPtr<ITreeItem>> Selection = TreeView->GetSelectedItems();
		if (Selection.Num() > 0)
		{
			TSharedPtr<ITreeItem> Selected = Selection[0]; // Single selection only

			const bool bShowMenu = Selected->GenerateContextMenu(MenuBuilder);

			if (bShowMenu)
			{
				return MenuBuilder.MakeWidget();
			}
		}

		return SNullWidget::NullWidget;
	}

	void SSetBinding::PostUndo(bool bSuccess)
	{
		RepopulateTreeData();
	}

	void SSetBinding::PostRedo(bool bSuccess)
	{
		RepopulateTreeData();
	}

	void SSetBinding::SetSetBinding(TWeakObjectPtr<UAbstractSkeletonSetBinding> InSetBinding)
	{
		SetBinding = InSetBinding;
		RepopulateTreeData();
	}

	void SSetBinding::RepopulateTreeData()
	{
		if (!bRepopulating)
		{
			TGuardValue<bool> RecursionGuard(bRepopulating, true);

			// Save the current expanded state of each tree item
			// - Only Set items can contain children so only Set items can be expanded/collapsed
			TSet<FName> ClosedSetCollections;
			{
				for (TSharedPtr<ITreeItem> TreeItem : GetAllTreeItems())
				{
					if (TreeItem->GetType() == FTreeItem_Set::StaticGetType() && !TreeView->IsItemExpanded(TreeItem))
					{
						TSharedPtr<FTreeItem_Set> SetTreeItem = StaticCastSharedPtr<FTreeItem_Set>(TreeItem);
						ClosedSetCollections.Add(SetTreeItem->Set.SetName);
					}
				}
			}
			
			// Rebuild tree data
			RootItems.Reset();

			if (SetBinding.IsValid())
			{
				TMap<FName, TSharedPtr<FTreeItem_Set>> TreeItemMap;
				
				if (SetBinding->GetSetCollection())
				{
					for (const FAbstractSkeletonSet& Set : SetBinding->GetSetCollection()->GetSetHierarchy())
					{
						TSharedPtr<FTreeItem_Set> SetItem = MakeShared<FTreeItem_Set>(*this, Set);

						if (Set.ParentSetName == NAME_None)
						{
							RootItems.Add(SetItem);
						}
						else
						{
							ensure(TreeItemMap.Contains(Set.ParentSetName));
							
							TSharedPtr<FTreeItem_Set> ParentItem = TreeItemMap[Set.ParentSetName];
							ParentItem->Children.Add(SetItem);
						}

						TreeItemMap.Add(Set.SetName, SetItem);
					}
				}

				for (const FAbstractSkeleton_BoneBinding& Binding : SetBinding->GetBoneBindings())
				{
					TSharedPtr<FTreeItem_Bone> BindingItem = MakeShared<FTreeItem_Bone>(*this, Binding);

					if (TreeItemMap.Contains(Binding.SetName))
					{
						TSharedPtr<FTreeItem_Set> SetItem = TreeItemMap[Binding.SetName];
						SetItem->Children.Insert(BindingItem, 0);
					}
					else
					{
						check(false);
					}
				}

				for (const FAbstractSkeleton_AttributeBinding& Binding : SetBinding->GetAttributeBindings())
				{
					TSharedPtr<FTreeItem_Attribute> BindingItem = MakeShared<FTreeItem_Attribute>(*this, Binding);

					if (TreeItemMap.Contains(Binding.SetName))
					{
						TSharedPtr<FTreeItem_Set> SetItem = TreeItemMap[Binding.SetName];
						SetItem->Children.Insert(BindingItem, 0);
					}
				}
			}

			TreeView->RebuildList();

			// Restore expanded/collapsed state
			{
				for (TSharedPtr<ITreeItem> TreeItem : GetAllTreeItems())
				{
					if (TreeItem->GetType() == FTreeItem_Set::StaticGetType())
					{
						const TSharedPtr<FTreeItem_Set> SetTreeItem = StaticCastSharedPtr<FTreeItem_Set>(TreeItem);
						const bool bShouldExpand = !ClosedSetCollections.Contains(SetTreeItem->Set.SetName);
						TreeView->SetItemExpansion(TreeItem, bShouldExpand);
					}
				}
			}

			OnTreeRefreshed.ExecuteIfBound();
		}
	}

	void SSetBinding::ExpandAllTreeItems()
	{
		for (TSharedPtr<ITreeItem> TreeItem : GetAllTreeItems())
		{
			TreeView->SetItemExpansion(TreeItem, true);
		}
	}

	void SSetBinding::BindCommands()
	{
		CommandList = MakeShareable(new FUICommandList);
	}

	TArray<TSharedPtr<SSetBinding::ITreeItem>> SSetBinding::GetAllTreeItems()
	{
		TArray<TSharedPtr<ITreeItem>> AllItems;
		AllItems.Append(RootItems);

		for (int32 Index = 0; Index < AllItems.Num(); ++Index)
		{
			AllItems[Index]->GetChildren(AllItems);
		}

		return AllItems;
	}
	
	void SSetBinding::FTreeItem_Set::GetChildren(TArray<TSharedPtr<ITreeItem>>& OutChildren)
	{
		OutChildren.Append(Children);
	}

	TSharedRef<ITableRow> SSetBinding::FTreeItem_Set::GenerateRow(const TSharedRef<STableViewBase>& OwnerTable)
	{
		return SNew(STableRow<TSharedPtr<ITreeItem>>, OwnerTable)
			.Style(FAppStyle::Get(), "TableView.AlternatingRow")
			.OnDragDetected_Lambda([this](const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
				{
					if (MouseEvent.IsMouseButtonDown(EKeys::LeftMouseButton))
					{
						const TSharedRef<FSetDragDropOp> DragDropOp = FSetDragDropOp::New(this);
						return FReply::Handled().BeginDragDrop(DragDropOp);
					}

					return FReply::Unhandled();
				})
			.OnCanAcceptDrop_Lambda([](const FDragDropEvent& DragDropEvent, EItemDropZone DropZone, TSharedPtr<ITreeItem> TargetItem) -> TOptional<EItemDropZone>
				{
					using namespace UE::UAF;

					TOptional<EItemDropZone> ReturnedDropZone;

					if (const TSharedPtr<FSkeletonTreeDragDropOp> DragDropOp = DragDropEvent.GetOperationAs<FSkeletonTreeDragDropOp>())
					{
						ReturnedDropZone = EItemDropZone::OntoItem;
					}

					if (const TSharedPtr<FAttributeDragDropOp> DragDropOp = DragDropEvent.GetOperationAs<FAttributeDragDropOp>())
					{
						ReturnedDropZone = EItemDropZone::OntoItem;
					}

					return ReturnedDropZone;
				})
			.OnAcceptDrop_Lambda([this](const FDragDropEvent& DragDropEvent, EItemDropZone DropZone, TSharedPtr<ITreeItem> TargetTreeItem) -> FReply
				{
					if (const TSharedPtr<FSkeletonTreeDragDropOp> DragDropOp = DragDropEvent.GetOperationAs<FSkeletonTreeDragDropOp>())
					{
						TSharedPtr<FTreeItem_Set> TargetSetTreeItem = StaticCastSharedPtr<FTreeItem_Set>(TargetTreeItem);

						const FScopedTransaction Transaction(LOCTEXT("AddAttributeToSet", "Add Attribute to Set"));
						SetBindingWidget.SetBinding->Modify();

						bool bTreeDirty = false;
									
						for (const TSharedPtr<SSetsSkeletonTree::FTreeItem>& Item : DragDropOp->Items)
						{
							bTreeDirty |= SetBindingWidget.SetBinding->RemoveBoneFromSet(Item->BoneName);
							bTreeDirty |= SetBindingWidget.SetBinding->AddBoneToSet(Item->BoneName, TargetSetTreeItem->Set.SetName);
						}
							
						if (bTreeDirty)
						{
							SetBindingWidget.RepopulateTreeData();
						}

						return FReply::Handled();
					}

					if (const TSharedPtr<FAttributeDragDropOp> DragDropOp = DragDropEvent.GetOperationAs<FAttributeDragDropOp>())
					{
						TSharedPtr<FTreeItem_Set> TargetSetTreeItem = StaticCastSharedPtr<FTreeItem_Set>(TargetTreeItem);
						
						const FScopedTransaction Transaction(LOCTEXT("AddAttributeToSet", "Add Attribute to Set"));
						SetBindingWidget.SetBinding->Modify();

						bool bTreeDirty = false;
									
						for (const FAnimationAttributeIdentifier& Attribute : DragDropOp->Attributes)
						{
							bTreeDirty |= SetBindingWidget.SetBinding->RemoveAttributeFromSet(Attribute);
							bTreeDirty |= SetBindingWidget.SetBinding->AddAttributeToSet(Attribute, TargetSetTreeItem->Set.SetName);
						}
							
						if (bTreeDirty)
						{
							SetBindingWidget.RepopulateTreeData();
						}

						return FReply::Handled();
					}
					
					return FReply::Unhandled();
				})
			.ShowWires(true)
			[
				SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(FMargin(3.0f))
					[
						SNew(SImage)
							.Image(FAppStyle::Get().GetBrush("ClassIcon.GroupActor"))
					]
					+ SHorizontalBox::Slot()
					.FillWidth(1.0)
					.AutoWidth()
					.Padding(3.0f)
					[
						SNew(STextBlock)
							.Text(FText::FromName(Set.SetName))
					]

			];
	}

	void SSetBinding::FTreeItem_Bone::GetChildren(TArray<TSharedPtr<ITreeItem>>& OutChildren)
	{
		// Bone items don't have children, do nothing
	}

	void SSetBinding::FTreeItem_Attribute::GetChildren(TArray<TSharedPtr<ITreeItem>>& OutChildren)
	{
		// Attribute items don't have children, do nothing
	}

	TSharedRef<ITableRow> SSetBinding::FTreeItem_Bone::GenerateRow(const TSharedRef<STableViewBase>& OwnerTable)
	{
		const FSlateBrush* Icon = FAppStyle::Get().GetBrush("SkeletonTree.Bone");	

		return SNew(STableRow<TSharedPtr<ITreeItem>>, OwnerTable)
			.Style(FAppStyle::Get(), "TableView.AlternatingRow")
			.OnCanAcceptDrop_Lambda([](const FDragDropEvent& DragDropEvent, EItemDropZone DropZone, TSharedPtr<ITreeItem> TargetItem) -> TOptional<EItemDropZone>
				{
					using namespace UE::UAF;

					TOptional<EItemDropZone> ReturnedDropZone;

					const TSharedPtr<FSkeletonTreeDragDropOp> DragDropOp = DragDropEvent.GetOperationAs<FSkeletonTreeDragDropOp>();
					if (DragDropOp.IsValid())
					{
						ReturnedDropZone = EItemDropZone::OntoItem;
					}

					return ReturnedDropZone;
				})
			.OnAcceptDrop_Lambda([this](const FDragDropEvent& DragDropEvent, EItemDropZone DropZone, TSharedPtr<ITreeItem> TargetTreeItem) -> FReply
				{
					const TSharedPtr<FSkeletonTreeDragDropOp> DragDropOp = DragDropEvent.GetOperationAs<FSkeletonTreeDragDropOp>();
					if (DragDropOp.IsValid())
					{
						const FScopedTransaction Transaction(LOCTEXT("AddAttributeToSet", "Add Attribute to Set"));
						SetBindingWidget.SetBinding->Modify();

						bool bTreeDirty = false;
									
						for (const TSharedPtr<SSetsSkeletonTree::FTreeItem>& Item : DragDropOp->Items)
						{
							bTreeDirty |= SetBindingWidget.SetBinding->RemoveBoneFromSet(Item->BoneName);
							bTreeDirty |= SetBindingWidget.SetBinding->AddBoneToSet(Item->BoneName, Binding.SetName);
						}
							
						if (bTreeDirty)
						{
							SetBindingWidget.RepopulateTreeData();
						}

						return FReply::Handled();
					}
					return FReply::Unhandled();
				})
			.ShowWires(true)
			[
				SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(FMargin(3.0f))
					[
						SNew(SImage)
							.Image(Icon)
							.ColorAndOpacity(FLinearColor(FColor(31, 228, 75)))
					]
					+ SHorizontalBox::Slot()
					.FillWidth(1.0)
					.AutoWidth()
					.Padding(3.0f)
					[
						SNew(STextBlock)
							.Text(FText::FromName(Binding.BoneName))
					]

			];
	}

	TSharedRef<ITableRow> SSetBinding::FTreeItem_Attribute::GenerateRow(const TSharedRef<STableViewBase>& OwnerTable)
	{
		const FSlateBrush* Icon = FAppStyle::Get().GetBrush("AnimGraph.Attribute.Attributes.Icon");

		return SNew(STableRow<TSharedPtr<ITreeItem>>, OwnerTable)
			.Style(FAppStyle::Get(), "TableView.AlternatingRow")
			.OnCanAcceptDrop_Lambda([](const FDragDropEvent& DragDropEvent, EItemDropZone DropZone, TSharedPtr<ITreeItem> TargetItem) -> TOptional<EItemDropZone>
				{
					using namespace UE::UAF;

					TOptional<EItemDropZone> ReturnedDropZone;

					const TSharedPtr<FSkeletonTreeDragDropOp> DragDropOp = DragDropEvent.GetOperationAs<FSkeletonTreeDragDropOp>();
					if (DragDropOp.IsValid())
					{
						ReturnedDropZone = EItemDropZone::OntoItem;
					}

					return ReturnedDropZone;
				})
			.OnAcceptDrop_Lambda([this](const FDragDropEvent& DragDropEvent, EItemDropZone DropZone, TSharedPtr<ITreeItem> TargetTreeItem) -> FReply
				{
					const TSharedPtr<FSkeletonTreeDragDropOp> DragDropOp = DragDropEvent.GetOperationAs<FSkeletonTreeDragDropOp>();
					if (DragDropOp.IsValid())
					{
						const FScopedTransaction Transaction(LOCTEXT("AddAttributeToSet", "Add Attribute to Set"));
						SetBindingWidget.SetBinding->Modify();

						bool bTreeDirty = false;
									
						for (const TSharedPtr<SSetsSkeletonTree::FTreeItem>& Item : DragDropOp->Items)
						{
							bTreeDirty |= SetBindingWidget.SetBinding->RemoveBoneFromSet(Item->BoneName);
							bTreeDirty |= SetBindingWidget.SetBinding->AddBoneToSet(Item->BoneName, Binding.SetName);
						}
							
						if (bTreeDirty)
						{
							SetBindingWidget.RepopulateTreeData();
						}

						return FReply::Handled();
					}
					return FReply::Unhandled();
				})
			.ShowWires(true)
			[
				SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(FMargin(1.0f, 1.0f))
					[
						SNew(SImage)
							.Image(Icon)
							.ColorAndOpacity(FLinearColor(FColor(31, 228, 75)))
					]
					+ SHorizontalBox::Slot()
					.FillWidth(1.0)
					.AutoWidth()
					.Padding(4.0f, 0.0f, 0.0f, 0.0f)
					[
						SNew(STextBlock)
							.Text(FText::FromName(Binding.Attribute.GetName()))
					]

			];
	}

	bool SSetBinding::FTreeItem_Set::GenerateContextMenu(FMenuBuilder& Menu)
	{
		return false;
	}

	bool SSetBinding::FTreeItem_Bone::GenerateContextMenu(FMenuBuilder& Menu)
	{
		FUIAction UnbindBone(
			FExecuteAction::CreateLambda([&]()
			{
				FScopedTransaction Transaction(LOCTEXT("RemoveBoneFromSet", "Remove Bone From Set"));
				SetBindingWidget.SetBinding->Modify();

				SetBindingWidget.SetBinding->RemoveBoneFromSet(Binding.BoneName);
				SetBindingWidget.RepopulateTreeData();
			})
		);

		Menu.AddMenuEntry(
			LOCTEXT("UnbindBone_Label", "Unbind Bone"),
			LOCTEXT("UnbindBone_Description", "Unbind the selected bone."),
			FSlateIcon(),
			UnbindBone);

		return true;
	}
	
	bool SSetBinding::FTreeItem_Attribute::GenerateContextMenu(FMenuBuilder& Menu)
	{
		FUIAction UnbindAttribute(
			FExecuteAction::CreateLambda([&]()
			{
				FScopedTransaction Transaction(LOCTEXT("RemoveAttributeFromSet", "Remove Attribute From Set"));
				SetBindingWidget.SetBinding->Modify();

				SetBindingWidget.SetBinding->RemoveAttributeFromSet(Binding.Attribute);
				SetBindingWidget.RepopulateTreeData();
			})
		);

		Menu.AddMenuEntry(
			LOCTEXT("UnbindAttribute_Label", "Unbind Attribute"),
			LOCTEXT("UnbindAttribute_Description", "Unbind the selected attribute."),
			FSlateIcon(),
			UnbindAttribute);

		return true;
	}

}

#undef LOCTEXT_NAMESPACE
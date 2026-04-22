// Copyright Epic Games, Inc. All Rights Reserved.

#include "VariablesOutlinerCategoryItem.h"

#include "AnimNextRigVMAsset.h"
#include "AnimNextRigVMAssetEditorData.h"
#include "ISceneOutliner.h"
#include "ScopedTransaction.h"
#include "UncookedOnlyUtils.h"
#include "VariablesOutlinerDragDrop.h"
#include "Entries/AnimNextRigVMAssetEntry.h"
#include "Styling/StarshipCoreStyle.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "Widgets/Views/STableRow.h"

#define LOCTEXT_NAMESPACE "VariablesOutlinerCategoryItem"

namespace UE::UAF::Editor
{
class FVariableDragDropOp;

const FSceneOutlinerTreeItemType FVariablesOutlinerCategoryItem::Type;

class SVariablesOutlinerCategoryLabel : FSceneOutlinerCommonLabelData, public SCompoundWidget
{
	SLATE_BEGIN_ARGS(SVariablesOutlinerCategoryLabel) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, FVariablesOutlinerCategoryItem& InTreeItem, ISceneOutliner& SceneOutliner)
	{
		WeakSceneOutliner = StaticCastSharedRef<ISceneOutliner>(SceneOutliner.AsShared());
		TreeItem = StaticCastSharedRef<FVariablesOutlinerCategoryItem>(InTreeItem.AsShared());
		
		ChildSlot
		[
			SNew(SHorizontalBox)
			+SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(2.0f, 2.0f)
			[
				SAssignNew(TextBlock, SInlineEditableTextBlock)
				.Font(FStyleFonts::Get().NormalBold)
				.Text(this, &SVariablesOutlinerCategoryLabel::GetDisplayText)
				.HighlightText(SceneOutliner.GetFilterHighlightText())
				.ColorAndOpacity(this, &SVariablesOutlinerCategoryLabel::GetForegroundColor)
				.OnTextCommitted(this, &SVariablesOutlinerCategoryLabel::OnTextCommited)
				.OnVerifyTextChanged(this, &SVariablesOutlinerCategoryLabel::OnVerifyTextChanged)
			]
		];
	}
	
	FText GetDisplayText() const
	{
		if (const TSharedPtr<FVariablesOutlinerCategoryItem> Item = TreeItem.Pin())
		{
			return FText::FromString(Item->GetDisplayString());
		}
		return FText();
	}

	virtual FSlateColor GetForegroundColor() const override
	{
		const TOptional<FLinearColor> BaseColor = FSceneOutlinerCommonLabelData::GetForegroundColor(*TreeItem.Pin());
		return BaseColor.IsSet() ? BaseColor.GetValue() : FSlateColor::UseForeground();
	}

	
	void OnTextCommited(const FText& InLabel, ETextCommit::Type InCommitInfo) const	
	{
		if(InCommitInfo == ETextCommit::OnEnter)
		{
			if (const TSharedPtr<FVariablesOutlinerCategoryItem> Item = TreeItem.Pin())
			{
				Item->Rename(InLabel); 
			}
		}
	}
	
	bool OnVerifyTextChanged(const FText& InLabel, FText& OutErrorMessage) const
	{
		if (const TSharedPtr<FVariablesOutlinerCategoryItem> Item = TreeItem.Pin())
		{
			return Item->ValidateName(InLabel, OutErrorMessage); 
		}
		return false;
	}

	virtual FReply OnDrop( const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent ) override
	{
		TSharedPtr<FVariableDragDropOp> GraphDropOp = DragDropEvent.GetOperationAs<FVariableDragDropOp>();
		if (GraphDropOp.IsValid())
		{
			if (const TSharedPtr<FVariablesOutlinerCategoryItem> CategoryItem = TreeItem.Pin())
			{
				TSharedPtr<FVariablesOutlinerEntryItem> EntryItem = GraphDropOp->WeakItem.Pin();
				if (EntryItem.IsValid())
				{
					if (UAnimNextVariableEntry* VariableEntry = EntryItem->WeakEntry.Get())
					{
						UAnimNextRigVMAsset* VariableAsset = VariableEntry->GetTypedOuter<UAnimNextRigVMAsset>();
						UAnimNextRigVMAsset* CategoryAsset = CategoryItem->WeakOwner.Get();

						const FText OtherVariableNameText = FText::FromName(VariableEntry->GetVariableName());
						const FText ThisVariableCategoryText = FText::FromString(CategoryItem->GetDisplayString());
						const FText FormattedMessage = FText::Format(LOCTEXT("MoveVariableToCategoryFormat", "Moving {0} to Category {1}"), OtherVariableNameText, ThisVariableCategoryText);	
						FScopedTransaction Transaction(FormattedMessage);

						UAnimNextVariableEntry* NewVariableEntry = VariableEntry;
						
						if (VariableAsset != CategoryAsset)
						{
							const FName VariableEntryName = VariableEntry->GetVariableName();
							UncookedOnly::FUtils::MoveVariableToAsset(VariableEntry, CategoryAsset, true, true);
							NewVariableEntry = Cast<UAnimNextVariableEntry>(UncookedOnly::FUtils::GetEditorData(CategoryAsset)->FindEntry(VariableEntryName));
						}
						
						check(NewVariableEntry);
						
						// Only add if not already part of category
						if (NewVariableEntry->GetVariableCategory() != CategoryItem->CategoryPath)
						{
							NewVariableEntry->SetVariableCategory(CategoryItem->CategoryPath);
						}
					}
				}
			}
			
			return FReply::Handled();
		}
		
		TSharedPtr<FCategoryDragDropOp> CategoryDropOp = DragDropEvent.GetOperationAs<FCategoryDragDropOp>();
		if (CategoryDropOp.IsValid())
		{
			if (const TSharedPtr<FVariablesOutlinerCategoryItem> CategoryItem = TreeItem.Pin())
			{
				TSharedPtr<FVariablesOutlinerCategoryItem> DropCategoryItem = CategoryDropOp->WeakItem.Pin();
				if (DropCategoryItem.IsValid())
				{
					const FString ThisCategoryName = CategoryItem->CategoryName;
					const FString OtherCategoryName = DropCategoryItem->CategoryName;

					UAnimNextRigVMAsset* ThisOwner = CategoryItem->WeakOwner.Get();
					const UAnimNextRigVMAsset* OtherOwner = DropCategoryItem->WeakOwner.Get();

					if (ThisOwner == OtherOwner)
					{
						const FText OtherCategoryText = FText::FromString(DropCategoryItem->GetDisplayString());
						const FText ThisCategoryText = FText::FromString(CategoryItem->GetDisplayString());
						
						const FText FormattedMessage = FText::Format(LOCTEXT("ReorderCategory", "Reorder {0} before {1}"), OtherCategoryText, ThisCategoryText);
						
						FScopedTransaction Transaction(FormattedMessage);
						UncookedOnly::FUtils::GetEditorData(ThisOwner)->ReorderCategory(OtherCategoryName, ThisCategoryName);
					}
				}
			}

			return FReply::Handled();
		}
		
		
		return FReply::Unhandled();
	}

	virtual void OnDragEnter( const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent ) override
	{
		TSharedPtr<FVariableDragDropOp> GraphDropOp = DragDropEvent.GetOperationAs<FVariableDragDropOp>();
		if (GraphDropOp.IsValid())
		{
			if (const TSharedPtr<FVariablesOutlinerCategoryItem> CategoryItem = TreeItem.Pin())
			{
				TSharedPtr<FVariablesOutlinerEntryItem> EntryItem = GraphDropOp->WeakItem.Pin();
				if (EntryItem.IsValid())
				{
					if (UAnimNextVariableEntry* VariableEntry = EntryItem->WeakEntry.Get())
					{
						const FText OtherVariableNameText = FText::FromName(VariableEntry->GetVariableName());
						const FText ThisVariableCategoryText = FText::FromString(CategoryItem->GetDisplayString());

						const UAnimNextRigVMAssetEditorData* VariableEditorData = VariableEntry->GetTypedOuter<UAnimNextRigVMAssetEditorData>();
						const UAnimNextRigVMAssetEditorData* CategoryEditorData = UncookedOnly::FUtils::GetEditorData(CategoryItem->WeakOwner.Get());

						if (VariableEditorData == CategoryEditorData)
						{
							if (VariableEntry->GetVariableCategory() == CategoryItem->CategoryName)
							{
								const FText FormattedMessage = FText::Format(LOCTEXT("VariableAlreadyPartOfCategoryFormat", "{0} is already part of Category {1}"), OtherVariableNameText, ThisVariableCategoryText);
								
								const FSlateBrush* ErrorSymbol = FAppStyle::GetBrush(TEXT("Graph.ConnectorFeedback.Error"));
								GraphDropOp->SetSimpleFeedbackMessage(ErrorSymbol, FSlateColor(GetForegroundColor()), FormattedMessage);
							}
							else if (!VariableEntry->GetVariableCategory().IsEmpty())
							{
								const FText FormattedMessage = FText::Format(LOCTEXT("MoveVariableToCategoryFormat", "Moving {0} to Category {1}"), OtherVariableNameText, ThisVariableCategoryText);							
								GraphDropOp->SetSimpleFeedbackMessage(nullptr, FSlateColor(GetForegroundColor()), FormattedMessage);
							}
							else
							{
								const FText FormattedMessage = FText::Format(LOCTEXT("AddVariableToCategoryFormat", "Add {0} to Category {1}"), OtherVariableNameText, ThisVariableCategoryText);
								GraphDropOp->SetSimpleFeedbackMessage(nullptr, FSlateColor(GetForegroundColor()), FormattedMessage);
							}
						}
						else
						{
							const FText ThisAssetText =  FText::FromString(CategoryItem->WeakOwner.Get()->GetName());
							const FText FormattedMessage = FText::Format(LOCTEXT("MoveVariableToCategoryAndAssetFormat", "Moving {0} Category {1} in {2}"), OtherVariableNameText, ThisVariableCategoryText, ThisAssetText);
							GraphDropOp->SetSimpleFeedbackMessage(nullptr, FSlateColor(GetForegroundColor()), FormattedMessage);						
						}
					}
				}
			}

			return;
		}
		
		TSharedPtr<FCategoryDragDropOp> CategoryDropOp = DragDropEvent.GetOperationAs<FCategoryDragDropOp>();
		if (CategoryDropOp.IsValid())
		{
			if (const TSharedPtr<FVariablesOutlinerCategoryItem> CategoryItem = TreeItem.Pin())
			{
				TSharedPtr<FVariablesOutlinerCategoryItem> DropCategoryItem = CategoryDropOp->WeakItem.Pin();
				if (DropCategoryItem.IsValid())
				{
					const UAnimNextRigVMAsset* ThisOwner = CategoryItem->WeakOwner.Get();
					const UAnimNextRigVMAsset* OtherOwner = DropCategoryItem->WeakOwner.Get();

					if (ThisOwner && ThisOwner == OtherOwner)
					{
						const FText OtherCategoryText = FText::FromString(DropCategoryItem->GetDisplayString());
						const FText ThisCategoryText = FText::FromString(CategoryItem->GetDisplayString());
						
						const FText FormattedMessage = FText::Format(LOCTEXT("ReorderCategory", "Reorder {0} before {1}"), OtherCategoryText, ThisCategoryText);
						CategoryDropOp->SetToolTip(FormattedMessage, nullptr);
					}
				}
			}
		}
	}

	virtual void OnDragLeave( const FDragDropEvent& DragDropEvent ) override
	{
		TSharedPtr<FVariableDragDropOp> GraphDropOp = DragDropEvent.GetOperationAs<FVariableDragDropOp>();
		if (GraphDropOp.IsValid())
		{
			GraphDropOp->HoverTargetChanged();
		}
	}

	TWeakPtr<FVariablesOutlinerCategoryItem> TreeItem;
	TSharedPtr<SInlineEditableTextBlock> TextBlock;
};

FVariablesOutlinerCategoryItem::FVariablesOutlinerCategoryItem(const FItemData& InItemData) : ISceneOutlinerTreeItem(FVariablesOutlinerCategoryItem::Type), WeakOwner(InItemData.InOwner), CategoryName(InItemData.InCategoryName), ParentCategoryName(InItemData.InParentCategoryName), CategoryPath(InItemData.InCategoryPath)
{
}

bool FVariablesOutlinerCategoryItem::IsValid() const
{
	return WeakOwner.Get() != nullptr;
}

FSceneOutlinerTreeItemID FVariablesOutlinerCategoryItem::GetID() const
{
	UAnimNextRigVMAsset* Owner = WeakOwner.Get();
	const FSoftObjectPath OwnerPath = Owner;
	return HashCombine(GetTypeHash(OwnerPath), GetTypeHash(CategoryPath));	
}

FString FVariablesOutlinerCategoryItem::GetDisplayString() const
{
	return CategoryName;
}

TSharedRef<SWidget> FVariablesOutlinerCategoryItem::GenerateLabelWidget(ISceneOutliner& Outliner, const STableRow<FSceneOutlinerTreeItemPtr>& InRow)
{
	TSharedRef<SVariablesOutlinerCategoryLabel> LabelWidget = SNew(SVariablesOutlinerCategoryLabel, *this, Outliner);
	RenameRequestEvent.BindSP(LabelWidget->TextBlock.Get(), &SInlineEditableTextBlock::EnterEditingMode);

	return LabelWidget;
}

FString FVariablesOutlinerCategoryItem::GetPackageName() const
{
	if (UAnimNextRigVMAsset* Owner = WeakOwner.Get())
	{
		return Owner->GetPackage()->GetName();
	}
	
	return ISceneOutlinerTreeItem::GetPackageName();
}

void FVariablesOutlinerCategoryItem::Rename(const FText& InNewName) const
{
	if(UAnimNextRigVMAsset* Owner = WeakOwner.Get())
	{
		if (UAnimNextRigVMAssetEditorData* EditorData = UE::UAF::UncookedOnly::FUtils::GetEditorData(Owner))
		{
			FScopedTransaction Transaction(LOCTEXT("RenameCategory", "Rename category"));
			EditorData->RenameCategory(CategoryName, InNewName.ToString());						
		}
	}	
}

bool FVariablesOutlinerCategoryItem::ValidateName(const FText& InNewName, FText& OutErrorMessage) const
{
	if(UAnimNextRigVMAsset* Owner = WeakOwner.Get())
	{
		if (UAnimNextRigVMAssetEditorData* EditorData = UE::UAF::UncookedOnly::FUtils::GetEditorData(Owner))
		{
			if (EditorData->VariableAndFunctionCategories.Contains(InNewName.ToString()))
			{
				OutErrorMessage = LOCTEXT("NameExistsError", "Category name already exists in this asset");
				return false;
			}
			
			return true;
		}
	}
	return false;
}
}

#undef LOCTEXT_NAMESPACE // "VariablesOutlinerCategoryItem"

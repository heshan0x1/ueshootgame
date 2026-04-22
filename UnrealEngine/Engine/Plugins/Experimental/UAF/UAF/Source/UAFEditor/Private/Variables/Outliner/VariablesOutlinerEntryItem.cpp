// Copyright Epic Games, Inc. All Rights Reserved.

#include "VariablesOutlinerEntryItem.h"

#include "ISceneOutliner.h"
#include "Styling/SlateColor.h"
#include "Misc/Optional.h"
#include "SceneOutlinerStandaloneTypes.h"
#include "AnimNextEditorModule.h"
#include "AnimNextRigVMAssetEditorData.h"
#include "EditorUtils.h"
#include "ScopedTransaction.h"
#include "UncookedOnlyUtils.h"
#include "Entries/AnimNextRigVMAssetEntry.h"
#include "UObject/Package.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/Views/SListView.h"
#include "VariablesOutlinerDragDrop.h"
#include "Entries/AnimNextVariableEntry.h"

#define LOCTEXT_NAMESPACE "VariablesOutlinerTreeItem"

namespace UE::UAF::Editor
{

const FSceneOutlinerTreeItemType FVariablesOutlinerEntryItem::Type;

class SVariablesOutlinerEntryLabel : FSceneOutlinerCommonLabelData, public SCompoundWidget
{
	SLATE_BEGIN_ARGS(SVariablesOutlinerEntryLabel) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, FVariablesOutlinerEntryItem& InTreeItem, ISceneOutliner& SceneOutliner, const STableRow<FSceneOutlinerTreeItemPtr>& InRow)
	{
		WeakSceneOutliner = StaticCastSharedRef<ISceneOutliner>(SceneOutliner.AsShared());
		TreeItem = StaticCastSharedRef<FVariablesOutlinerEntryItem>(InTreeItem.AsShared());

		ChildSlot
		[
			SNew(SHorizontalBox)
			+SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(2.0f, 2.0f)
			[
				SAssignNew(TextBlock, SInlineEditableTextBlock)
				.IsReadOnly(this, &SVariablesOutlinerEntryLabel::IsReadOnly)
				.Text(this, &SVariablesOutlinerEntryLabel::GetDisplayText)
				.HighlightText(SceneOutliner.GetFilterHighlightText())
				.ColorAndOpacity(this, &SVariablesOutlinerEntryLabel::GetForegroundColor)
				.OnTextCommitted(this, &SVariablesOutlinerEntryLabel::OnTextCommited)
				.OnVerifyTextChanged(this, &SVariablesOutlinerEntryLabel::OnVerifyTextChanged)
			]
			+SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.Padding(2.0f, 0.0f, 2.0f, 3.0f)
			.AutoWidth()
			[
				SNew(SImage)
				.ToolTipText(this, &SVariablesOutlinerEntryLabel::GetDirtyTooltipText)
				.Image(this, &SVariablesOutlinerEntryLabel::GetDirtyImageBrush)
			]
		];
	}

	FText GetDirtyTooltipText() const
	{
		if (const TSharedPtr<FVariablesOutlinerEntryItem> Item = TreeItem.Pin())
		{
			FTextBuilder TextBuilder;
			TextBuilder.AppendLine(LOCTEXT("ModifiedTooltip", "Modified"));

			if(UAnimNextVariableEntry* AssetEntry = Item->WeakEntry.Get())
			{
				const UPackage* ExternalPackage = AssetEntry->GetExternalPackage();
				if(ExternalPackage && ExternalPackage->IsDirty())
				{
					TextBuilder.AppendLine(FText::FromName(ExternalPackage->GetFName()));
				}
			}

			return TextBuilder.ToText();
		}
		return FText::GetEmpty();
	}

	const FSlateBrush* GetDirtyImageBrush() const
	{
		if (const TSharedPtr<FVariablesOutlinerEntryItem> Item = TreeItem.Pin())
		{
			if(UAnimNextVariableEntry* AssetEntry = Item->WeakEntry.Get())
			{
				bool bIsDirty = false;
				const UPackage* ExternalPackage = AssetEntry->GetExternalPackage();
				if(ExternalPackage && ExternalPackage->IsDirty())
				{
					bIsDirty = true;
				}

				return bIsDirty ? FAppStyle::GetBrush("Icons.DirtyBadge") : nullptr;
			}
		}
		return nullptr;
	}
	
	FText GetDisplayText() const
	{
		if (const TSharedPtr<FVariablesOutlinerEntryItem> Item = TreeItem.Pin())
		{
			return FText::FromString(Item->GetDisplayString());
		}
		return FText();
	}

	void OnTextCommited(const FText& InLabel, ETextCommit::Type InCommitInfo) const
	{
		if(InCommitInfo == ETextCommit::OnEnter)
		{
			if (const TSharedPtr<FVariablesOutlinerEntryItem> Item = TreeItem.Pin())
			{
				Item->Rename(InLabel); 
			}
		}
	}

	bool OnVerifyTextChanged(const FText& InLabel, FText& OutErrorMessage) const
	{
		if (const TSharedPtr<FVariablesOutlinerEntryItem> Item = TreeItem.Pin())
		{
			return Item->ValidateName(InLabel, OutErrorMessage); 
		}
		return false;
	}

	bool IsReadOnly() const
	{
		if (const TSharedPtr<FVariablesOutlinerEntryItem> Item = TreeItem.Pin())
		{
			return !Item->PropertyPath.IsPathToFieldEmpty() || Item->WeakEntry.Get() == nullptr;
		}
		return false;
	}
	
	virtual FSlateColor GetForegroundColor() const override
	{
		const TOptional<FLinearColor> BaseColor = FSceneOutlinerCommonLabelData::GetForegroundColor(*TreeItem.Pin());
		return BaseColor.IsSet() ? BaseColor.GetValue() : FSlateColor::UseForeground();
	}
	
	virtual FReply OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override
	{
		TSharedPtr<FVariableDragDropOp> GraphDropOp = DragDropEvent.GetOperationAs<FVariableDragDropOp>();
		if (GraphDropOp.IsValid())
		{
			if (const TSharedPtr<FVariablesOutlinerEntryItem> ThisEntryItem = TreeItem.Pin())
			{
				const UAnimNextVariableEntry* ThisEntry = ThisEntryItem->WeakEntry.Get();
				TSharedPtr<FVariablesOutlinerEntryItem> EntryItem = GraphDropOp->WeakItem.Pin();
				if (ThisEntryItem->HasStructOwner())
				{
					const FSlateBrush* ErrorSymbol = FAppStyle::GetBrush(TEXT("Graph.ConnectorFeedback.Error"));
					GraphDropOp->SetSimpleFeedbackMessage(ErrorSymbol, FSlateColor(GetForegroundColor()), LOCTEXT("StructItemFeedback", "Cannot make changes to Struct Entries"));
				}
				else if (EntryItem.IsValid())
				{
					UAnimNextVariableEntry* OtherEntry = EntryItem->WeakEntry.Get();
					if (ThisEntry && OtherEntry && ThisEntry != OtherEntry)
					{
						UAnimNextRigVMAsset* ThisAsset = ThisEntry->GetTypedOuter<UAnimNextRigVMAsset>();
						const UAnimNextRigVMAsset* OtherAsset = OtherEntry->GetTypedOuter<UAnimNextRigVMAsset>();
						if (ThisAsset && ThisAsset == OtherAsset)
						{
							if (ThisEntry->GetVariableCategory() != OtherEntry->GetVariableCategory())
							{
								if (OtherEntry && ThisEntry)
								{
									UAnimNextRigVMAssetEditorData* VariableEditorData = OtherEntry->GetTypedOuter<UAnimNextRigVMAssetEditorData>();
									UAnimNextRigVMAssetEditorData* ThisEditorData = ThisEntry->GetTypedOuter<UAnimNextRigVMAssetEditorData>();

									if (VariableEditorData && ThisEditorData)
									{
										if (VariableEditorData == ThisEditorData)
										{
											// Add or remove to category of this variable entry
											const bool bRemovingFromCategory = ThisEntry->GetVariableCategory().IsEmpty();

											const FText OtherVariableNameText = FText::FromName(OtherEntry->GetVariableName());					
											const FText OtherVariableCategoryText = FText::FromString(OtherEntry->GetVariableCategory().GetData());
											const FText ThisVariableNameText = FText::FromName(ThisEntry->GetVariableName());
											const FText ThisVariableCategoryText = FText::FromString(ThisEntry->GetVariableCategory().GetData());											
											const FText AddFormattedMessage = FText::Format(LOCTEXT("AddVariableToCategoryFormat", "Add {0} to Category {1}"), OtherVariableNameText, ThisVariableCategoryText);
											const FText RemoveFormattedMessage = FText::Format(LOCTEXT("RemoveVariableToCategoryFormat", "Remove {0} from Category {1}"), OtherVariableNameText, OtherVariableCategoryText);
											
											FScopedTransaction Transaction(bRemovingFromCategory ? RemoveFormattedMessage : AddFormattedMessage);
											OtherEntry->SetVariableCategory(ThisEntry->GetVariableCategory(), true);		
										}
									}
								}
							}
							else if (OtherEntry)
							{
								UAnimNextVariableEntry* VariableEntry = EntryItem->WeakEntry.Get();
								const UAnimNextVariableEntry* ThisVariableEntry = ThisEntryItem->WeakEntry.Get();
								if (VariableEntry && ThisVariableEntry)
								{
									const FText OtherVariableNameText = FText::FromName(OtherEntry->GetVariableName());					
									const FText ThisVariableNameText = FText::FromName(ThisEntry->GetVariableName());
									const FText FormattedMessage = FText::Format(LOCTEXT("ReorderVariableFormat", "Reorder {0} before {1}"), OtherVariableNameText, ThisVariableNameText);
									FScopedTransaction Transaction(FormattedMessage);
									
									UAnimNextRigVMAssetEditorData* VariableEditorData = VariableEntry->GetTypedOuter<UAnimNextRigVMAssetEditorData>();
									UAnimNextRigVMAssetEditorData* ThisEditorData = ThisVariableEntry->GetTypedOuter<UAnimNextRigVMAssetEditorData>();
									if (VariableEditorData && VariableEditorData == ThisEditorData)
									{
										VariableEditorData->ReorderVariable(VariableEntry, ThisVariableEntry);
									}
								}								
							}
						}
						else if (ThisAsset)
						{
							if (UAnimNextVariableEntry* VariableEntry = EntryItem->WeakEntry.Get())
							{
								const FText VariableNameText = FText::FromName(OtherEntry->GetVariableName());
								const FText AssetNameText = FText::FromName(ThisAsset->GetFName());
																		
								const FText FormattedMessage = FText::Format(LOCTEXT("MoveVariableToAssetFormatTransaction", "Moving {0} to {1}"), VariableNameText, AssetNameText);
								FScopedTransaction Transaction(FormattedMessage);
								UncookedOnly::FUtils::MoveVariableToAsset(VariableEntry, ThisAsset, true, true);
							}
						}	
					}
				}
			}
		}
		
		return FReply::Unhandled();
	}

	virtual void OnDragEnter(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override
	{
		TSharedPtr<FVariableDragDropOp> GraphDropOp = DragDropEvent.GetOperationAs<FVariableDragDropOp>();
		if (GraphDropOp.IsValid())
		{
			if (const TSharedPtr<FVariablesOutlinerEntryItem> ThisEntryItem = TreeItem.Pin())
			{
				const UAnimNextVariableEntry* ThisEntry = ThisEntryItem->WeakEntry.Get();
				TSharedPtr<FVariablesOutlinerEntryItem> EntryItem = GraphDropOp->WeakItem.Pin();

				const FSlateBrush* ErrorSymbol = FAppStyle::GetBrush(TEXT("Graph.ConnectorFeedback.Error"));
				if (ThisEntryItem->HasStructOwner())
				{
					GraphDropOp->SetSimpleFeedbackMessage(ErrorSymbol, FSlateColor(GetForegroundColor()), LOCTEXT("StructItemFeedback", "Cannot make changes to Struct Entries"));
				}
				else if (ThisEntry && EntryItem.IsValid())
				{
					if (const UAnimNextVariableEntry* OtherEntry = EntryItem->WeakEntry.Get())
					{
						const FText OtherVariableNameText = FText::FromName(OtherEntry->GetVariableName());					
						const FText OtherVariableCategoryText = FText::FromString(OtherEntry->GetVariableCategory().GetData());
						const FText ThisVariableNameText = FText::FromName(ThisEntry->GetVariableName());
						const FText ThisVariableCategoryText = FText::FromString(ThisEntry->GetVariableCategory().GetData());
						
						if (ThisEntry && ThisEntry != OtherEntry)
						{
							UAnimNextRigVMAsset* ThisAsset = ThisEntryItem->WeakEntry.Get()->GetTypedOuter<UAnimNextRigVMAsset>();
							const UAnimNextRigVMAsset* OtherAsset = EntryItem->WeakEntry.Get()->GetTypedOuter<UAnimNextRigVMAsset>();
							if (ThisAsset && ThisAsset == OtherAsset)
							{
								if (ThisEntry->GetVariableCategory() != OtherEntry->GetVariableCategory())
								{
									if (ThisEntry->GetVariableCategory().IsEmpty())
									{
										// Remove from category
										const FText FormattedMessage = FText::Format(LOCTEXT("RemoveVariableToCategoryFormat", "Remove {0} from Category {1}"), OtherVariableNameText, OtherVariableCategoryText);
										GraphDropOp->SetSimpleFeedbackMessage(nullptr, FSlateColor(GetForegroundColor()), FormattedMessage);
									}
									else
									{
										// Add to category
										const FText FormattedMessage = FText::Format(LOCTEXT("AddVariableToCategoryFormat", "Add {0} to Category {1}"), OtherVariableNameText, ThisVariableCategoryText);
										GraphDropOp->SetSimpleFeedbackMessage(nullptr, FSlateColor(GetForegroundColor()), FormattedMessage);
									}
								}
								else
								{
									// Reorder
									const FText FormattedMessage = FText::Format(LOCTEXT("ReorderVariableFormat", "Reorder {0} before {1}"), OtherVariableNameText, ThisVariableNameText);
									GraphDropOp->SetSimpleFeedbackMessage(nullptr, FSlateColor(GetForegroundColor()), FormattedMessage);
								}
							}
							else if (ThisAsset)
							{
								// Reorder
								GraphDropOp->SetSimpleFeedbackMessage(nullptr, FSlateColor(GetForegroundColor()), LOCTEXT("ReorderDifferentOuterVariableLabel", "Cannot Reorder Variable in different Asset"));

								const FText VariableNameText = FText::FromName(OtherEntry->GetVariableName());
								const FText AssetNameText = FText::FromName(ThisAsset->GetFName());
								
								// [TODO] should we only allow move variables "up" the asset chain? If so validate that according to the workspace export chain, that VariableOuter is a child of Asset
								const FText FormattedMessage = FText::Format(LOCTEXT("MoveVariableToAssetFormat", "Move {0} to {1}"), VariableNameText, AssetNameText);
								GraphDropOp->SetSimpleFeedbackMessage(nullptr, FSlateColor(GetForegroundColor()), FormattedMessage);
							}						
						}
						else
						{
							// Reorder
							GraphDropOp->SetSimpleFeedbackMessage(ErrorSymbol, FSlateColor(GetForegroundColor()), LOCTEXT("ReorderSelfVariableLabel", "Cannot Reorder Variable before itself"));
						}
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

	TWeakPtr<FVariablesOutlinerEntryItem> TreeItem;
	TSharedPtr<SInlineEditableTextBlock> TextBlock;
};

FVariablesOutlinerEntryItem::FVariablesOutlinerEntryItem(UAnimNextVariableEntry* InEntry)
	: ISceneOutlinerTreeItem(FVariablesOutlinerEntryItem::Type)
	, WeakEntry(InEntry)
{
}

FVariablesOutlinerEntryItem::FVariablesOutlinerEntryItem(const FProperty* InProperty)
	: ISceneOutlinerTreeItem(FVariablesOutlinerEntryItem::Type)
	, PropertyPath(InProperty)
	, bStructOwner(true)
{
}

bool FVariablesOutlinerEntryItem::IsValid() const
{
	return WeakEntry.Get() != nullptr || PropertyPath.Get() != nullptr;
}

FSceneOutlinerTreeItemID FVariablesOutlinerEntryItem::GetID() const
{
	if (UAnimNextVariableEntry* Entry = WeakEntry.Get())
	{
		return HashCombine(GetTypeHash(Entry), GetTypeHash(WeakSharedVariablesEntry.Get()));
	}
	else
	{
		return HashCombine(GetTypeHash(PropertyPath), GetTypeHash(WeakSharedVariablesEntry.Get()));
	}
}

FString FVariablesOutlinerEntryItem::GetDisplayString() const
{
	if(UAnimNextRigVMAssetEntry* Entry = WeakEntry.Get())
	{
		return Entry->GetDisplayName().ToString();
	}
	else if (const FProperty* Property = PropertyPath.Get())
	{
		return Property->GetName();
	}

	return FString();
}

TSharedRef<SWidget> FVariablesOutlinerEntryItem::GenerateLabelWidget(ISceneOutliner& Outliner, const STableRow<FSceneOutlinerTreeItemPtr>& InRow)
{
	TSharedRef<SVariablesOutlinerEntryLabel> LabelWidget = SNew(SVariablesOutlinerEntryLabel, *this, Outliner, InRow);
	RenameRequestEvent.BindSP(LabelWidget->TextBlock.Get(), &SInlineEditableTextBlock::EnterEditingMode);
	return LabelWidget;
}

FString FVariablesOutlinerEntryItem::GetPackageName() const
{
	if(UAnimNextRigVMAssetEntry* Entry = WeakEntry.Get())
	{
		return Entry->GetPackage()->GetName();
	}
	else if (const FProperty* Property = PropertyPath.Get())
	{
		return Property->GetOwner<UScriptStruct>()->GetPackage()->GetName();
	}
	
	return ISceneOutlinerTreeItem::GetPackageName();
}

void FVariablesOutlinerEntryItem::Rename(const FText& InNewName) const
{
	if(UAnimNextRigVMAssetEntry* Entry = WeakEntry.Get())
	{
		FScopedTransaction Transaction(LOCTEXT("RenameVariable", "Rename variable"));
		const FName NewName = FName(*InNewName.ToString());
		UncookedOnly::FUtils::RenameVariable(CastChecked<UAnimNextVariableEntry>(Entry), NewName);
	}
}

bool FVariablesOutlinerEntryItem::ValidateName(const FText& InNewName, FText& OutErrorMessage) const
{
	UAnimNextVariableEntry* Entry = WeakEntry.Get();
	if(Entry == nullptr)
	{
		OutErrorMessage = LOCTEXT("InvalidVariableError", "Variable is invalid");
		return false;
	}

	UAnimNextRigVMAssetEditorData* EditorData = Entry->GetTypedOuter<UAnimNextRigVMAssetEditorData>();
	if(EditorData == nullptr)
	{
		OutErrorMessage = LOCTEXT("InvalidEditorDataError", "Variable has invalid editor data");
		return false;
	}

	const FString NewString = InNewName.ToString();
	if (!Editor::FUtils::IsValidParameterNameString(NewString, OutErrorMessage))
	{
		return false;
	}

	FName Name(*NewString);
	// Cannot have two entries with matching names
	UAnimNextRigVMAssetEntry* ExistingEntry = EditorData->FindEntry(Name);
	if(ExistingEntry && ExistingEntry != Entry)
	{
		OutErrorMessage = LOCTEXT("NameExistsError", "Variable name already exists in this asset");
		return false;
	}

	return true;
}

}

#undef LOCTEXT_NAMESPACE // "VariablesOutlinerTreeItem"
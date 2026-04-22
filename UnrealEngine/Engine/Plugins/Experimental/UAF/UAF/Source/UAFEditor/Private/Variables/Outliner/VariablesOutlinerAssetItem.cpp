// Copyright Epic Games, Inc. All Rights Reserved.

#include "VariablesOutlinerAssetItem.h"

#include "ISceneOutliner.h"
#include "Styling/SlateColor.h"
#include "Misc/Attribute.h"
#include "Misc/Optional.h"
#include "SceneOutlinerStandaloneTypes.h"
#include "UAFStyle.h"
#include "AnimNextRigVMAsset.h"
#include "AnimNextRigVMAssetEditorData.h"
#include "AssetToolsModule.h"
#include "AssetViewUtils.h"
#include "IAssetTools.h"
#include "ScopedTransaction.h"
#include "UncookedOnlyUtils.h"
#include "VariablesOutlinerDragDrop.h"
#include "VariablesOutlinerMode.h"
#include "WorkspaceDragDropOperation.h"
#include "Entries/AnimNextSharedVariablesEntry.h"
#include "Styling/SlateIconFinder.h"
#include "Styling/StarshipCoreStyle.h"
#include "UObject/Package.h"
#include "Variables/AnimNextVariableItemMenuContext.h"
#include "Variables/SVariablesView.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Images/SThrobber.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/Layout/SSpacer.h"

#define LOCTEXT_NAMESPACE "VariablesOutlinerTreeItem"

namespace UE::UAF::Editor
{

const FSceneOutlinerTreeItemType FVariablesOutlinerAssetItem::Type;

class SVariablesOutlinerAssetLabel : FSceneOutlinerCommonLabelData, public SCompoundWidget
{
	SLATE_BEGIN_ARGS(SVariablesOutlinerAssetLabel) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, FVariablesOutlinerAssetItem& InTreeItem, ISceneOutliner& SceneOutliner, const STableRow<FSceneOutlinerTreeItemPtr>& InRow)
	{
		WeakSceneOutliner = StaticCastSharedRef<ISceneOutliner>(SceneOutliner.AsShared());
		TreeItem = StaticCastSharedRef<FVariablesOutlinerAssetItem>(InTreeItem.AsShared());
		
		TWeakPtr<SVariablesOutlinerAssetLabel> WeakLabel = StaticCastSharedRef<SVariablesOutlinerAssetLabel>(AsShared());
		ChildSlot
		[
			SNew(SHorizontalBox)
			+SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(2.0f, 2.0f)
			[
				SNew(SImage)
				.Image(this, &SVariablesOutlinerAssetLabel::GetAssetIcon)
			]
			+SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(2.0f, 2.0f)
			[
				SAssignNew(TextBlock, SInlineEditableTextBlock)
				.Font(FStyleFonts::Get().NormalBold)
				.Text(this, &SVariablesOutlinerAssetLabel::GetDisplayText)
				.HighlightText(SceneOutliner.GetFilterHighlightText())
				.ColorAndOpacity(this, &SVariablesOutlinerAssetLabel::GetForegroundColor)
				.OnTextCommitted(this, &SVariablesOutlinerAssetLabel::OnTextCommited)
				.OnVerifyTextChanged(this, &SVariablesOutlinerAssetLabel::OnVerifyTextChanged)
				.IsReadOnly(this, &SVariablesOutlinerAssetLabel::CanRename)
			]
			+SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.Padding(2.0f, 0.0f, 2.0f, 3.0f)
			.AutoWidth()
			[
				SNew(SImage)
				.Visibility(this, &SVariablesOutlinerAssetLabel::GetDirtyImageVisibility)
				.ToolTipText(this, &SVariablesOutlinerAssetLabel::GetDirtyTooltipText)
				.Image(this, &SVariablesOutlinerAssetLabel::GetDirtyImageBrush)
			]
			+SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.Padding(2.0f, 0.0f)
			.AutoWidth()
			[
				SNew(SBox)
				.Visibility(this, &SVariablesOutlinerAssetLabel::GetAddVariableButtonVisibility)
				[
					SNew(SComboButton)
					.ContentPadding(FMargin(0, 0))
					.ButtonStyle(FAppStyle::Get(), "SimpleButton")
					.HasDownArrow(false)
					.ButtonContent()
					[
						SNew(SImage)
						.Image(FAppStyle::Get().GetBrush("Icons.PlusCircle"))
						.ColorAndOpacity(FSlateColor::UseForeground())
					]
					.OnGetMenuContent_Lambda([WeakOutliner = SceneOutliner.AsShared().ToWeakPtr(), WeakItem = InTreeItem.AsShared().ToWeakPtr()]() -> TSharedRef<SWidget>
					{
						TSharedPtr<SVariablesOutliner> SharedOutliner = StaticCastSharedPtr<SVariablesOutliner>(WeakOutliner.Pin());
						ensure(SharedOutliner.IsValid());

						if (const FVariablesOutlinerMode* VariablesMode = static_cast<const FVariablesOutlinerMode*>(SharedOutliner->GetMode()))
						{
							FToolMenuContext Context;
							UAnimNextVariableItemMenuContext* MenuContext = NewObject<UAnimNextVariableItemMenuContext>();
							MenuContext->WeakOutliner = SharedOutliner;	

							TSharedPtr<FVariablesOutlinerAssetItem> SharedAssetItem = StaticCastSharedPtr<FVariablesOutlinerAssetItem>(WeakItem.Pin());
							if (SharedAssetItem.IsValid())
							{
								if (UAnimNextRigVMAsset* Asset = SharedAssetItem->SoftAsset.Get())
								{
									if (UAnimNextRigVMAssetEditorData* EditorData = UncookedOnly::FUtils::GetEditorData(Asset))
									{
										MenuContext->WeakEditorDatas.Add(EditorData);
									}
								}
							}

							Context.AddObject(MenuContext);
							static const FName MenuName("VariablesOutliner.AddVariablesMenu");
							return UToolMenus::Get()->GenerateWidget(MenuName, Context);
						}

						return SNullWidget::NullWidget;
					})
				]
			]
			+SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.Padding(0.0f, 0.0f)
			.AutoWidth()
			[
				SAssignNew(ImplementedSharedVariablesHBox, SHorizontalBox)
				.Clipping(EWidgetClipping::OnDemand)
			]
			
			+SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.Padding(2.0f, 0.0f)
			.AutoWidth()
			[
				SNew(SThrobber)
				.Visibility(this, &SVariablesOutlinerAssetLabel::GetLoadingIndicatorVisibility)
				.ToolTipText(LOCTEXT("LoadingTooltip", "Asset is loading..."))
			]
		];
		
		if (const TSharedPtr<FVariablesOutlinerAssetItem> Item = TreeItem.Pin())
		{
			for (const FSoftObjectPath& SharedVariablesPath : Item->SharedVariableSourcePaths)
			{
				const FString SharedVariablesAssetName = SharedVariablesPath.GetAssetName();
				const FLinearColor BackgroundColor = FLinearColor::MakeRandomSeededColor(GetTypeHash(SharedVariablesAssetName)).Desaturate(0.4f);

				const FButtonStyle& CloseButtonStyle = FUAFStyle::Get().GetWidgetStyle<FButtonStyle>("VariablesOutliner.SharedVariablesPill.CloseButton");
				TSharedPtr<SBorder> SharedVariablesBorderWidget;
				ImplementedSharedVariablesHBox->AddSlot()
				.VAlign(VAlign_Center)
				.Padding(2.f, 0.f)
				.AutoWidth()
				[
					SAssignNew(SharedVariablesBorderWidget, SBorder)
					.BorderImage(FUAFStyle::Get().GetBrush("VariablesOutliner.SharedVariablesPill.Border"))
					.BorderBackgroundColor(BackgroundColor)
					.ToolTipText( FText::FromString(SharedVariablesAssetName))
					.VAlign(VAlign_Center)
					.Padding(FMargin(6.0f, 1.0f, 4.0f, 1.0f))
					[
						SNew(SHorizontalBox)
						+SHorizontalBox::Slot()
						.AutoWidth()
						.HAlign(HAlign_Center)
						.VAlign(VAlign_Center)
						[
							SNew(STextBlock)
							.TextStyle(FUAFStyle::Get(), "VariablesOutliner.SharedVariablesPill.Text")
							.Text(FText::FromString(SharedVariablesAssetName))
							.HighlightText(SceneOutliner.GetFilterHighlightText())
							.OverflowPolicy(ETextOverflowPolicy::MiddleEllipsis)
						]
						+SHorizontalBox::Slot()
						.HAlign(HAlign_Right)
						.VAlign(VAlign_Center)
						.AutoWidth()
						[
							SNew(SButton)
							.ButtonStyle(&CloseButtonStyle)
							.OnClicked( Item.ToSharedRef(), &FVariablesOutlinerAssetItem::OnRemoveSharedVariable, SharedVariablesPath )
							.ContentPadding(FMargin(0.0))
							.ToolTipText(LOCTEXT("RemoveReference_ToolTip", "Removes the SharedVariables reference from this Asset."))
							[
								SNew(SSpacer)
								.Size(CloseButtonStyle.Normal.ImageSize)
							]
						]
					]
					.OnMouseButtonUp(Item.ToSharedRef(), &FVariablesOutlinerAssetItem::OnSharedVariableWidgetMouseUp, SharedVariablesPath)
				];

				SharedVariablesBorderWidget->SetOnMouseMove(FPointerEventHandler::CreateLambda([SharedVariablesPath, WeakOutliner = WeakSceneOutliner](const FGeometry&, const FPointerEvent& Event) -> FReply
				{
					const bool bShouldHighlight = Event.GetModifierKeys().IsControlDown();
					{
						if (TSharedPtr<ISceneOutliner> SharedOutliner = WeakOutliner.Pin())
						{
							if (const FVariablesOutlinerMode* Mode = static_cast<const FVariablesOutlinerMode*>(SharedOutliner->GetMode()))
							{
								if (bShouldHighlight)
								{
									Mode->SetHighlightedItem(GetTypeHash(SharedVariablesPath));
								}
								else
								{
									Mode->ClearHighlightedItem(GetTypeHash(SharedVariablesPath));
								}
							}
						}
					}

					return FReply::Handled();
				}));

				SharedVariablesBorderWidget->SetOnMouseLeave(FSimpleNoReplyPointerEventHandler::CreateLambda([SharedVariablesPath, WeakOutliner = WeakSceneOutliner](const FPointerEvent&)
				{
					if (TSharedPtr<ISceneOutliner> SharedOutliner = WeakOutliner.Pin())
					{
						if (const FVariablesOutlinerMode* Mode = static_cast<const FVariablesOutlinerMode*>(SharedOutliner->GetMode()))
						{
							Mode->ClearHighlightedItem(GetTypeHash(SharedVariablesPath));					
						}
					}
				}));
			}
		}
	}

	bool CanRename() const
	{
		if (const TSharedPtr<FVariablesOutlinerAssetItem> Item = TreeItem.Pin())
		{
			return Item->SoftAsset.Get() != nullptr;
		}
		return false;
	}

	FText GetDirtyTooltipText() const
	{
		if (const TSharedPtr<FVariablesOutlinerAssetItem> Item = TreeItem.Pin())
		{
			FTextBuilder TextBuilder;
			TextBuilder.AppendLine(LOCTEXT("ModifiedTooltip", "Modified"));

			if(UAnimNextRigVMAsset* Asset = Item->SoftAsset.Get())
			{
				const UPackage* Package = Asset->GetPackage();
				check(Package);
				if(Package->IsDirty())
				{
					TextBuilder.AppendLine(FText::FromName(Package->GetFName()));
				}
			}

			return TextBuilder.ToText();
		}
		return FText::GetEmpty();
	}

	const FSlateBrush* GetDirtyImageBrush() const
	{
		if (const TSharedPtr<FVariablesOutlinerAssetItem> Item = TreeItem.Pin())
		{
			if(UAnimNextRigVMAsset* Asset = Item->SoftAsset.Get())
			{
				bool bIsDirty = false;
				const UPackage* Package = Asset->GetPackage();
				check(Package);
				if(Package->IsDirty())
				{
					bIsDirty = true;
				}

				return bIsDirty ? FAppStyle::GetBrush("Icons.DirtyBadge") : nullptr;
			}
		}
		return nullptr;
	}

	EVisibility GetDirtyImageVisibility() const
	{
		if (const TSharedPtr<FVariablesOutlinerAssetItem> Item = TreeItem.Pin())
		{
			if(UAnimNextRigVMAsset* Asset = Item->SoftAsset.Get())
			{
				bool bIsDirty = false;
				const UPackage* Package = Asset->GetPackage();
				check(Package);
				if(Package->IsDirty())
				{
					bIsDirty = true;
				}

				return bIsDirty ? EVisibility::Visible : EVisibility::Collapsed;
			}
		}
		return EVisibility::Collapsed;
	}

	EVisibility GetLoadingIndicatorVisibility() const
	{
		if (const TSharedPtr<FVariablesOutlinerAssetItem> Item = TreeItem.Pin())
		{
			UAnimNextRigVMAsset* Asset = Item->SoftAsset.Get();
			return Asset != nullptr ? EVisibility::Collapsed : EVisibility::Visible;
		}
		return EVisibility::Collapsed;
	}
	
	FText GetDisplayText() const
	{
		if (const TSharedPtr<FVariablesOutlinerAssetItem> Item = TreeItem.Pin())
		{
			return FText::FromString(Item->GetDisplayString());
		}
		return FText();
	}

	const FSlateBrush* GetAssetIcon() const
	{
		if (const TSharedPtr<FVariablesOutlinerAssetItem> Item = TreeItem.Pin())
		{
			UAnimNextRigVMAsset* Asset = Item->SoftAsset.Get();
			return Asset != nullptr ? FSlateIconFinder::FindIconBrushForClass(Asset->GetClass()) : FAppStyle::Get().GetBrush("ClassIcon.Object");
		}
		return FAppStyle::Get().GetBrush("ClassIcon.Object");
	}

	void OnTextCommited(const FText& InLabel, ETextCommit::Type InCommitInfo) const
	{
		if(InCommitInfo == ETextCommit::OnEnter)
		{
			if (const TSharedPtr<FVariablesOutlinerAssetItem> Item = TreeItem.Pin())
			{
				Item->Rename(InLabel); 
			}
		}
	}

	bool OnVerifyTextChanged(const FText& InLabel, FText& OutErrorMessage) const
	{
		if (const TSharedPtr<FVariablesOutlinerAssetItem> Item = TreeItem.Pin())
		{
			return Item->ValidateName(InLabel, OutErrorMessage); 
		}
		return false;
	}

	virtual FSlateColor GetForegroundColor() const override
	{
		const TOptional<FLinearColor> BaseColor = FSceneOutlinerCommonLabelData::GetForegroundColor(*TreeItem.Pin());
		return BaseColor.IsSet() ? BaseColor.GetValue() : FSlateColor::UseForeground();
	}
	
	EVisibility GetAddVariableButtonVisibility() const
	{
		if (const TSharedPtr<FVariablesOutlinerAssetItem> Item = TreeItem.Pin())
		{
			if(Item->SoftAsset.Get())
			{
				return EVisibility::Visible;
			}
		}
		return EVisibility::Collapsed;
	}

	virtual FReply OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent ) override
	{
		TSharedPtr<FVariableDragDropOp> GraphDropOp = DragDropEvent.GetOperationAs<FVariableDragDropOp>();
		if (GraphDropOp.IsValid())
		{
			if (UAnimNextRigVMAsset* Asset = TreeItem.Pin()->SoftAsset.Get())
			{
				TSharedPtr<FVariablesOutlinerEntryItem> EntryItem = GraphDropOp->WeakItem.Pin();
				if (EntryItem.IsValid())
				{
					if (UAnimNextVariableEntry* VariableEntry = EntryItem->WeakEntry.Get())
					{
						const UAnimNextRigVMAsset* VariableOuter = VariableEntry->GetTypedOuter<UAnimNextRigVMAsset>();
						if (VariableOuter != Asset)
						{
							const FText VariableNameText = FText::FromName(VariableEntry->GetVariableName());
							const FText AssetNameText = FText::FromName(Asset->GetFName());
							
							const FText FormattedMessage = FText::Format(LOCTEXT("MoveVariableToAssetFormatTransaction", "Moving {0} to {1}"), VariableNameText, AssetNameText);
							FScopedTransaction Transaction(FormattedMessage);
							UncookedOnly::FUtils::MoveVariableToAsset(VariableEntry, Asset, true, true);

							return FReply::Handled();
						}
					}
				}
			}
		}

		TSharedPtr<FWorkspaceDragDropOp> WorkspaceDragDrop  = DragDropEvent.GetOperationAs<FWorkspaceDragDropOp>();
		if (WorkspaceDragDrop.IsValid())
		{
			if (UAnimNextRigVMAsset* Asset = TreeItem.Pin()->SoftAsset.Get())
			{
				const FText AssetNameText = FText::FromName(Asset->GetFName());

				UAnimNextRigVMAssetEditorData* EditorData = UncookedOnly::FUtils::GetEditorData<UAnimNextRigVMAssetEditorData>(Asset);
				TArray<const UAnimNextSharedVariables*> SharedVariableAssetsToAdd;				
				for (const FAssetData& AssetData : WorkspaceDragDrop->AssetDatas)
				{					
					const UClass* AssetClass = AssetData.GetClass(EResolveClass::Yes);
					if (AssetClass && AssetClass->IsChildOf<UAnimNextSharedVariables>())
					{						
						if (!EditorData->FindEntry(AssetData.AssetName))
						{
							if (const UAnimNextSharedVariables* SharedVariables = Cast<UAnimNextSharedVariables>(AssetData.GetAsset()))
							{
								SharedVariableAssetsToAdd.Add(SharedVariables);
							}
						}
					}
				}

				if (SharedVariableAssetsToAdd.Num())
				{
					const FText FormattedMessage = FText::Format(LOCTEXT("AddSharedVariablesToAssetTransaction", "Adding Shared Variables to {0}"), AssetNameText);
					FScopedTransaction Transaction(FormattedMessage);
					for (const UAnimNextSharedVariables* SharedVariables : SharedVariableAssetsToAdd)
					{
						EditorData->AddSharedVariables(SharedVariables);
					}

					return FReply::Handled();
				}
			}
		}
		
		return FReply::Unhandled();
	}

	virtual void OnDragEnter( const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent ) override
	{
		TSharedPtr<FVariableDragDropOp> GraphDropOp = DragDropEvent.GetOperationAs<FVariableDragDropOp>();
		if (GraphDropOp.IsValid())
		{			
			if (const UAnimNextRigVMAsset* Asset = TreeItem.Pin()->SoftAsset.Get())
			{
				TSharedPtr<FVariablesOutlinerEntryItem> EntryItem = GraphDropOp->WeakItem.Pin();
				if (EntryItem.IsValid())
				{
					if (const UAnimNextVariableEntry* VariableEntry = EntryItem->WeakEntry.Get())
					{
						const FText VariableNameText = FText::FromName(VariableEntry->GetVariableName());
						const FText AssetNameText = FText::FromName(Asset->GetFName());
						
						const UAnimNextRigVMAsset* VariableOuter = VariableEntry->GetTypedOuter<UAnimNextRigVMAsset>();
						if (VariableOuter != Asset)
						{
							// [TODO] should we only allow move variables "up" the asset chain? If so validate that according to the workspace export chain, that VariableOuter is a child of Asset
							const FText FormattedMessage = FText::Format(LOCTEXT("MoveVariableToAssetFormat", "Move {0} to {1}"), VariableNameText, AssetNameText);
							GraphDropOp->SetSimpleFeedbackMessage(nullptr, FSlateColor(GetForegroundColor()), FormattedMessage);				
						}
						else
						{
							const FSlateBrush* ErrorSymbol = FAppStyle::GetBrush(TEXT("Graph.ConnectorFeedback.Error"));
							const FText FormattedMessage = FText::Format(LOCTEXT("VariableAlreadyPartOfAssetFormat", "{0} is already part of {1}"), VariableNameText, AssetNameText);
							GraphDropOp->SetSimpleFeedbackMessage(ErrorSymbol, FSlateColor(GetForegroundColor()), FormattedMessage);		
						}
					}
				}
			}
		}

		TSharedPtr<FWorkspaceDragDropOp> WorkspaceDragDrop  = DragDropEvent.GetOperationAs<FWorkspaceDragDropOp>();
		if (WorkspaceDragDrop.IsValid())
		{
			if (const UAnimNextRigVMAsset* Asset = TreeItem.Pin()->SoftAsset.Get())
			{
				const FText AssetNameText = FText::FromName(Asset->GetFName());

				TArray<FName> SharedVariableAssetNamesToAdd;
				TArray<FName> PreexistingSharedVariableAssetNames;
				
				for (const FAssetData& AssetData : WorkspaceDragDrop->AssetDatas)
				{					
					const UClass* AssetClass = AssetData.GetClass(EResolveClass::Yes);
					if (AssetClass && AssetClass->IsChildOf<UAnimNextSharedVariables>())
					{						
						const UAnimNextRigVMAssetEditorData* EditorData = UncookedOnly::FUtils::GetEditorData<const UAnimNextRigVMAssetEditorData>(Asset);
						if (!EditorData->FindEntry(AssetData.AssetName))
						{
							SharedVariableAssetNamesToAdd.Add(AssetData.AssetName);
						}
						else
						{

							PreexistingSharedVariableAssetNames.Add(AssetData.AssetName);
						}
					}
				}

				const bool bAddingSharedVariables = SharedVariableAssetNamesToAdd.Num() > 0;
				const bool bPreexistingSharedVariables = PreexistingSharedVariableAssetNames.Num() > 0;

				if (bPreexistingSharedVariables && !bAddingSharedVariables)
				{
					const bool bMultipleAssets = PreexistingSharedVariableAssetNames.Num() > 1;
					
					const FSlateBrush* ErrorSymbol = FAppStyle::GetBrush(TEXT("Graph.ConnectorFeedback.Error"));
					const FText SingleFormattedMessage = FText::Format(LOCTEXT("ExistingSharedVariablesInAsset", "{0} already references {1}"), AssetNameText, FText::FromName(PreexistingSharedVariableAssetNames[0]));
					const FText MultiFormattedMessage = FText::Format(LOCTEXT("ExistingSharedVariablesInAssets", "{0} already references assets"), AssetNameText);

					WorkspaceDragDrop->SetToolTip(bMultipleAssets ? MultiFormattedMessage : SingleFormattedMessage, ErrorSymbol);
				}
				else if (bAddingSharedVariables)
				{
					if (SharedVariableAssetNamesToAdd.Num() > 1)
					{
						FString SharedVariablesNameListString;
						for (const FName& AssetName : SharedVariableAssetNamesToAdd)
						{
							if (!SharedVariablesNameListString.IsEmpty())
							{
								SharedVariablesNameListString.Append(TEXT(", "));
							}

							SharedVariablesNameListString.Appendf(TEXT("%s"), *AssetName.ToString());
						}
						
						const FText SharedVariablesNameListText = FText::FromString(SharedVariablesNameListString);
						const FText MultiFormattedMessage = FText::Format(LOCTEXT("AddMultipleSharedVariablesToAsset", "Add {0} to {1}"), SharedVariablesNameListText, AssetNameText);

						WorkspaceDragDrop->SetToolTip(MultiFormattedMessage, FSlateIconFinder::FindIconBrushForClass(UAnimNextSharedVariables::StaticClass()));
					}
					else
					{
						const FText SharedVariablesNameText = FText::FromName(SharedVariableAssetNamesToAdd[0]);
						const FText SingleFormattedMessage = FText::Format(LOCTEXT("AddSharedVariablesToAsset", "Add {0} to {1}"), SharedVariablesNameText, AssetNameText);
						WorkspaceDragDrop->SetToolTip(SingleFormattedMessage, FSlateIconFinder::FindIconBrushForClass(UAnimNextSharedVariables::StaticClass()));
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

	TWeakPtr<FVariablesOutlinerAssetItem> TreeItem;
	TSharedPtr<SInlineEditableTextBlock> TextBlock;
	TSharedPtr<SHorizontalBox> ImplementedSharedVariablesHBox;
};

FVariablesOutlinerAssetItem::FVariablesOutlinerAssetItem(const FItemData& InItemData)
	: ISceneOutlinerTreeItem(FVariablesOutlinerAssetItem::Type)
	, SoftAsset(InItemData.Asset)
	, SortValue(InItemData.SortValue)
	, SharedVariableSourcePaths(InItemData.ImplementedSharedVariablesPaths)
{
}

bool FVariablesOutlinerAssetItem::IsValid() const
{
	return !SoftAsset.IsNull();
}

FSceneOutlinerTreeItemID FVariablesOutlinerAssetItem::GetID() const
{
	return GetTypeHash(SoftAsset.ToSoftObjectPath());
}

FString FVariablesOutlinerAssetItem::GetDisplayString() const
{
	return SoftAsset.GetAssetName();
}

TSharedRef<SWidget> FVariablesOutlinerAssetItem::GenerateLabelWidget(ISceneOutliner& Outliner, const STableRow<FSceneOutlinerTreeItemPtr>& InRow)
{
	SAssignNew(AssetLabel, SVariablesOutlinerAssetLabel, *this, Outliner, InRow);
	RenameRequestEvent.BindSP(AssetLabel->TextBlock.Get(), &SInlineEditableTextBlock::EnterEditingMode);
	return AssetLabel->AsShared();
}

FString FVariablesOutlinerAssetItem::GetPackageName() const
{
	return SoftAsset.GetLongPackageName();
}

void FVariablesOutlinerAssetItem::Rename(const FText& InNewName) const
{
	UAnimNextRigVMAsset* Asset = SoftAsset.Get();
	if(Asset == nullptr)
	{
		return;
	}

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
	const FString CurrentAssetPath = FPackageName::GetLongPackagePath(Asset->GetPackage()->GetName());
	TArray<FAssetRenameData> AssetsToRename = { FAssetRenameData(Asset, CurrentAssetPath, InNewName.ToString()) };
	AssetToolsModule.Get().RenameAssets(AssetsToRename);
}

bool FVariablesOutlinerAssetItem::ValidateName(const FText& InNewName, FText& OutErrorMessage) const
{
	UAnimNextRigVMAsset* Asset = SoftAsset.Get();
	if(Asset == nullptr)
	{
		OutErrorMessage = LOCTEXT("InvalidAssetError", "Asset is invalid");
		return false;
	}

	FString NewName = InNewName.ToString();
	if (NewName.Len() >= NAME_SIZE)
	{
		OutErrorMessage = LOCTEXT("Error_AssetNameTooLarge", "This asset name is too long. Please choose a shorter name.");
		return false;
	}

	if (Asset->GetFName() != FName(*NewName)) // Deliberately ignore case here to allow case-only renames of existing assets
	{
		const FString PackageName = Asset->GetPackage()->GetPathName() / NewName;
		const FString ObjectPath = FString::Printf(TEXT("%s.%s"), *PackageName, *NewName);

		FText ValidationErrorMsg;
		if (!AssetViewUtils::IsValidObjectPathForCreate(ObjectPath, ValidationErrorMsg))
		{
			OutErrorMessage = ValidationErrorMsg;
			return false;
		}
	}

	FAssetToolsModule& AssetToolsModule = FAssetToolsModule::GetModule();
	TWeakPtr<IAssetTypeActions> WeakAssetTypeActions = AssetToolsModule.Get().GetAssetTypeActionsForClass(UAnimNextRigVMAsset::StaticClass());
	if (TSharedPtr<IAssetTypeActions> AssetTypeActions = WeakAssetTypeActions.Pin())
	{
		if (!AssetTypeActions->CanRename(FAssetData(Asset), &OutErrorMessage))
		{
			return false;
		}
	}

	return true;
}

FReply FVariablesOutlinerAssetItem::OnSharedVariableWidgetMouseUp(const FGeometry& Geometry, const FPointerEvent& PointerEvent, const FSoftObjectPath ClickedSharedVariablesPath) const
{
	if (PointerEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		const FWidgetPath WidgetPath = (PointerEvent.GetEventPath() != nullptr) ? *PointerEvent.GetEventPath() : FWidgetPath();

		const bool bCloseAfterSelection = true;
		FMenuBuilder MenuBuilder(bCloseAfterSelection, nullptr);
		MenuBuilder.BeginSection("", LOCTEXT("SharedVariablesContextMenu", "Shared Variables") );
		{
			MenuBuilder.AddMenuEntry(
					LOCTEXT("RemoveReference", "Remove"),
					LOCTEXT("RemoveReference_ToolTip", "Removes the SharedVariables reference from this Asset."),
					FSlateIcon(),
					FUIAction(FExecuteAction::CreateLambda([this, ClickedSharedVariablesPath]() { OnRemoveSharedVariable(ClickedSharedVariablesPath); }))
			);
		}
		MenuBuilder.EndSection();

		FSlateApplication::Get().PushMenu(AssetLabel.ToSharedRef(), WidgetPath, MenuBuilder.MakeWidget(), FSlateApplication::Get().GetCursorPos(), FPopupTransitionEffect(FPopupTransitionEffect::ContextMenu));
		
		return FReply::Handled();
	}

	return FReply::Unhandled();
}

FReply FVariablesOutlinerAssetItem::OnRemoveSharedVariable(const FSoftObjectPath ClickedSharedVariablesPath) const
{
	if (UAnimNextRigVMAsset* Asset = SoftAsset.Get())
	{
		const FText AssetNameText = FText::FromName(Asset->GetFName());

		UAnimNextRigVMAssetEditorData* EditorData = UncookedOnly::FUtils::GetEditorData<UAnimNextRigVMAssetEditorData>(Asset);
		if (UAnimNextSharedVariablesEntry* SharedVariableEntryToRemove = Cast<UAnimNextSharedVariablesEntry>(EditorData->FindEntry(ClickedSharedVariablesPath.GetAssetFName())))
		{
			const FText SharedVariablesAssetNameText = FText::FromName(ClickedSharedVariablesPath.GetAssetFName());
			const FText FormattedMessage = FText::Format(LOCTEXT("RemovingSharedAssetEntryFormat", "Removing {0} from {1}"), SharedVariablesAssetNameText, AssetNameText);
			FScopedTransaction Transaction(FormattedMessage);
			
			if (const UAnimNextSharedVariables* SharedVariablesToRemove = SharedVariableEntryToRemove->GetAsset())
			{
				// Replace all referenced variables from the to-be-removed SharedVariables from the referencing Asset
				FAnimNextAssetRegistryExports VariableExports;
				UncookedOnly::FUtils::GetExportedVariablesForAsset(SharedVariablesToRemove, VariableExports);

				VariableExports.ForEachExportOfType<FAnimNextVariableDeclarationData>([EditorData, ClickedSharedVariablesPath](const FName& Identifier, const FAnimNextVariableDeclarationData& VariableDeclaration) -> bool
				{
					FAnimNextSoftVariableReference VariableReference(Identifier, ClickedSharedVariablesPath);
					FAnimNextSoftVariableReference EmptyReference;
					UncookedOnly::FUtils::ReplaceVariableReferences(EditorData, VariableReference, EmptyReference);
				
					return true;
				});
			}
			else if (const UScriptStruct* Struct = SharedVariableEntryToRemove->GetStruct())
			{
				// Replace all referenced variables from the to-be-removed Native SharedVariables source from the referencing Asset
				for (TFieldIterator<FProperty> It(Struct); It; ++It)
				{
					const FProperty* Property = *It;
					if (Property->HasAnyPropertyFlags(CPF_NativeAccessSpecifierPublic))
					{

						FAnimNextSoftVariableReference VariableReference(Property->GetFName(), ClickedSharedVariablesPath);
						FAnimNextSoftVariableReference EmptyReference;
						UncookedOnly::FUtils::ReplaceVariableReferences(EditorData, VariableReference, EmptyReference);
					}
				}
			}

			// Remove the entry itself
			EditorData->RemoveEntry(SharedVariableEntryToRemove);

			return FReply::Handled();
		}
	}

	return FReply::Unhandled();
}
}

#undef LOCTEXT_NAMESPACE // "VariablesOutlinerTreeItem"
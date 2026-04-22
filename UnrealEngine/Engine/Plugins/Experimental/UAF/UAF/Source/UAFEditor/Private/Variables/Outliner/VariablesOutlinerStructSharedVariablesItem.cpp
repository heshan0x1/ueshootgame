// Copyright Epic Games, Inc. All Rights Reserved.

#include "VariablesOutlinerStructSharedVariablesItem.h"

#include "ISceneOutliner.h"
#include "Styling/SlateColor.h"
#include "Misc/Attribute.h"
#include "Misc/Optional.h"
#include "SceneOutlinerStandaloneTypes.h"
#include "AnimNextEditorModule.h"
#include "AnimNextRigVMAssetEditorData.h"
#include "VariablesOutlinerDragDrop.h"
#include "Entries/AnimNextSharedVariablesEntry.h"
#include "Styling/SlateIconFinder.h"
#include "Styling/StarshipCoreStyle.h"
#include "UObject/Package.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/Views/SListView.h"

#define LOCTEXT_NAMESPACE "VariablesOutlinerTreeItem"

namespace UE::UAF::Editor
{

const FSceneOutlinerTreeItemType FVariablesOutlinerStructSharedVariablesItem::Type;

class SVariablesOutlinerStructSharedVariablesLabel : FSceneOutlinerCommonLabelData, public SCompoundWidget
{
	SLATE_BEGIN_ARGS(SVariablesOutlinerStructSharedVariablesLabel) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, FVariablesOutlinerStructSharedVariablesItem& InTreeItem, ISceneOutliner& SceneOutliner, const STableRow<FSceneOutlinerTreeItemPtr>& InRow)
	{
		WeakSceneOutliner = StaticCastSharedRef<ISceneOutliner>(SceneOutliner.AsShared());
		TreeItem = StaticCastSharedRef<FVariablesOutlinerStructSharedVariablesItem>(InTreeItem.AsShared());

		FText AssetName;
		if(const UAnimNextSharedVariablesEntry* AssetEntry = InTreeItem.WeakEntry.Get())
		{
			AssetName = FText::FromString(AssetEntry->GetObjectPath().ToString());
		}
		else
		{
			AssetName = LOCTEXT("UnknownAssetName", "Unknown Asset");
		}
		
		ChildSlot
		[
			SNew(SHorizontalBox)
			.ToolTipText(FText::Format(LOCTEXT("ImportedVariablesFormat", "Shared variables from '{0}'"), AssetName))
			+SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(2.0f, 2.0f)
			[
				SNew(SImage)
				.Image(FSlateIconFinder::FindIconBrushForClass(UUserDefinedStruct::StaticClass()))
			]
			+SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(2.0f, 2.0f)
			[
				SNew(STextBlock)
				.Font(FStyleFonts::Get().NormalBold)
				.Text(this, &SVariablesOutlinerStructSharedVariablesLabel::GetDisplayText)
				.HighlightText(SceneOutliner.GetFilterHighlightText())
				.ColorAndOpacity(this, &SVariablesOutlinerStructSharedVariablesLabel::GetForegroundColor)
			]
		];
	}
	
	FText GetDisplayText() const
	{
		if (const TSharedPtr<FVariablesOutlinerStructSharedVariablesItem> Item = TreeItem.Pin())
		{
			return FText::FromString(Item->GetDisplayString());
		}
		return FText();
	}

	virtual void OnDragEnter( const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent ) override
	{
		TSharedPtr<FVariableDragDropOp> GraphDropOp = DragDropEvent.GetOperationAs<FVariableDragDropOp>();
		if (GraphDropOp.IsValid())
		{
			const FSlateBrush* ErrorSymbol = FAppStyle::GetBrush(TEXT("Graph.ConnectorFeedback.Error"));
			GraphDropOp->SetSimpleFeedbackMessage(ErrorSymbol, FSlateColor(GetForegroundColor()), LOCTEXT("StructSharedVariablesFeedback", "Cannot make changes to Struct SharedVariables"));
		}
	}

	virtual FSlateColor GetForegroundColor() const override
	{
		const TOptional<FLinearColor> BaseColor = FSceneOutlinerCommonLabelData::GetForegroundColor(*TreeItem.Pin());
		return BaseColor.IsSet() ? BaseColor.GetValue() : FSlateColor::UseForeground();
	}

	TWeakPtr<FVariablesOutlinerStructSharedVariablesItem> TreeItem;
	TSharedPtr<SInlineEditableTextBlock> TextBlock;
};

FVariablesOutlinerStructSharedVariablesItem::FVariablesOutlinerStructSharedVariablesItem(UAnimNextSharedVariablesEntry* InEntry)
	: ISceneOutlinerTreeItem(FVariablesOutlinerStructSharedVariablesItem::Type)
	, WeakEntry(InEntry)
{
}

bool FVariablesOutlinerStructSharedVariablesItem::IsValid() const
{
	return WeakEntry.Get() != nullptr;
}

FSceneOutlinerTreeItemID FVariablesOutlinerStructSharedVariablesItem::GetID() const
{
	if (const UAnimNextSharedVariablesEntry* SharedVariableEntry = WeakEntry.Get())
	{
		check(SharedVariableEntry->GetStruct());
		const FSoftObjectPath Path = SharedVariableEntry->GetStruct();
		return GetTypeHash(Path);
	}
	
	return GetTypeHash(WeakEntry);
}

FString FVariablesOutlinerStructSharedVariablesItem::GetDisplayString() const
{
	const UAnimNextRigVMAssetEntry* Entry = WeakEntry.Get();
	if(Entry == nullptr)
	{
		return FString();
	}

	return Entry->GetDisplayName().ToString();
}

TSharedRef<SWidget> FVariablesOutlinerStructSharedVariablesItem::GenerateLabelWidget(ISceneOutliner& Outliner, const STableRow<FSceneOutlinerTreeItemPtr>& InRow)
{
	return SNew(SVariablesOutlinerStructSharedVariablesLabel, *this, Outliner, InRow);
}

FString FVariablesOutlinerStructSharedVariablesItem::GetPackageName() const
{
	const UAnimNextRigVMAssetEntry* Entry = WeakEntry.Get();
	if(Entry == nullptr)
	{
		return ISceneOutlinerTreeItem::GetPackageName();
	}

	return Entry->GetPackage()->GetName();
}

}

#undef LOCTEXT_NAMESPACE // "VariablesOutlinerTreeItem"
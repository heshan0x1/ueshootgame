// Copyright Epic Games, Inc. All Rights Reserved.

#include "Columns/NavigationToolLengthColumn.h"
#include "INavigationToolView.h"
#include "Widgets/Columns/SNavigationToolLength.h"
#include "Widgets/SNavigationToolTreeRow.h"
#include "Widgets/Views/SHeaderRow.h"

#define LOCTEXT_NAMESPACE "NavigationToolLengthColumn"

namespace UE::SequenceNavigator
{

UE_SEQUENCER_DEFINE_CASTABLE(FNavigationToolLengthColumn)

FText FNavigationToolLengthColumn::GetColumnDisplayNameText() const
{
	return LOCTEXT("LengthColumn", "Length");
}

const FSlateBrush* FNavigationToolLengthColumn::GetIconBrush() const
{
	return FAppStyle::GetBrush(TEXT("CurveEd.FitHorizontal"));
}

SHeaderRow::FColumn::FArguments FNavigationToolLengthColumn::ConstructHeaderRowColumn(const TSharedRef<INavigationToolView>& InView
	, const float InFillSize)
{
	const FName ColumnId = GetColumnId();

	return SHeaderRow::Column(ColumnId)
		.FillWidth(InFillSize)
		.HAlignHeader(HAlign_Center)
		.VAlignHeader(VAlign_Center)
		.HAlignCell(HAlign_Fill)
		.VAlignCell(VAlign_Center)
		.DefaultLabel(GetColumnDisplayNameText())
		.OnGetMenuContent(InView, &INavigationToolView::GetColumnMenuContent, ColumnId);
}

TSharedRef<SWidget> FNavigationToolLengthColumn::ConstructRowWidget(const FNavigationToolViewModelPtr& InItem
	, const TSharedRef<INavigationToolView>& InView
	, const TSharedRef<SNavigationToolTreeRow>& InRow)
{
	return SNew(SNavigationToolLength, InItem.ImplicitCast(), InView, InRow);
}

} // namespace UE::SequenceNavigator

#undef LOCTEXT_NAMESPACE

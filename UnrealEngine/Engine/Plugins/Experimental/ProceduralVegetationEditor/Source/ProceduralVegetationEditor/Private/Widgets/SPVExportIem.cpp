// Copyright Epic Games, Inc. All Rights Reserved.
#include "SPVExportItem.h"
#include "Widgets/Layout/SBox.h"
#include <Widgets/Text/STextBlock.h>

#define LOCTEXT_NAMESPACE "SPVExportItem"

void SPVExportItem::Construct(const FArguments& InArgs)
{
	OnOutputSelectionChanged = InArgs._OnOutputSelectionChanged;
	bIsSelected = InArgs._bIsSelected;
	auto& Name = InArgs._Name;

	auto OnSelectionChanged = [this, Name](ECheckBoxState NewState)
	{
		bIsSelected = NewState == ECheckBoxState::Checked;
		OnOutputSelectionChanged.ExecuteIfBound(Name.ToString(), NewState);
	};

	ChildSlot
	[
		SNew(SHorizontalBox)

		+ SHorizontalBox::Slot()
		[
			SNew(SCheckBox)
			.IsChecked(this, &SPVExportItem::OnCheckBoxState)
			.OnCheckStateChanged_Lambda(OnSelectionChanged)
			.Content()
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.VAlign(VAlign_Center)
				.Padding(5)
				[
					SNew(STextBlock)
					.Text(InArgs._Name)
				]
			]
		]
	];
}

ECheckBoxState SPVExportItem::OnCheckBoxState() const
{
	return bIsSelected ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

#undef LOCTEXT_NAMESPACE
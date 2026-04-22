// Copyright Epic Games, Inc. All Rights Reserved.

#include "UAF/AbstractSkeleton/Sets/SetDragDrop.h"

namespace UE::UAF::Sets
{
	TSharedRef<FSetDragDropOp> FSetDragDropOp::New(SSetBinding::FTreeItem_Set* InItem)
	{
		TSharedRef<FSetDragDropOp> Operation = MakeShared<FSetDragDropOp>();
		Operation->Item = InItem;

		Operation->Construct();

		return Operation;
	}

	TSharedPtr<SWidget> FSetDragDropOp::GetDefaultDecorator() const
	{
		return SNew(SBorder)
			.Visibility(EVisibility::Visible)
			.BorderImage(FAppStyle::GetBrush("Menu.Background"))
			[
				SNew(STextBlock)
					.Text(FText::FromName(Item->Set.SetName))
			];
	}
}
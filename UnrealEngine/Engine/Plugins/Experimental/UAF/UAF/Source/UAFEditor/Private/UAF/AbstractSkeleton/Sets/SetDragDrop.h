// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DragAndDrop/DecoratedDragDropOp.h"
#include "UAF/AbstractSkeleton/Sets/SSetBinding.h"

namespace UE::UAF::Sets
{
	class FSetDragDropOp : public FDecoratedDragDropOp
	{
	public:
		DRAG_DROP_OPERATOR_TYPE(FSetDragDropOp, FDecoratedDragDropOp)

		static TSharedRef<FSetDragDropOp> New(SSetBinding::FTreeItem_Set* InItem);

		TSharedPtr<SWidget> GetDefaultDecorator() const override;

		SSetBinding::FTreeItem_Set* Item;
	};
}

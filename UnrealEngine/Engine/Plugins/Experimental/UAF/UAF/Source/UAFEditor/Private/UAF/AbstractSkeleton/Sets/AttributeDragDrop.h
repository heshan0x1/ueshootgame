// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DragAndDrop/DecoratedDragDropOp.h"
#include "Animation/AnimData/AttributeIdentifier.h"

namespace UE::UAF::Sets
{
	class FAttributeDragDropOp : public FDecoratedDragDropOp
	{
	public:
		DRAG_DROP_OPERATOR_TYPE(FAttributeDragDropOp, FDecoratedDragDropOp)

		static TSharedRef<FAttributeDragDropOp> New(TArray<FAnimationAttributeIdentifier>&& InAttributes);

		TSharedPtr<SWidget> GetDefaultDecorator() const override;

		TArray<FAnimationAttributeIdentifier> Attributes;
	};
}

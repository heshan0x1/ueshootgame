// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Types/SlateEnums.h"
#include "DragBoxPosition.generated.h"

/** Used to restore position of widget in SDraggableBoxOverlay. */
USTRUCT()
struct FToolWidget_DragBoxPosition
{
	GENERATED_BODY()

	UPROPERTY()
	FVector2f RelativeOffset = FVector2f::ZeroVector;

	UPROPERTY()
	TEnumAsByte<EHorizontalAlignment> HAlign = HAlign_Left;

	UPROPERTY()
	TEnumAsByte<EVerticalAlignment> VAlign = VAlign_Bottom;

	FToolWidget_DragBoxPosition() = default;
	explicit FToolWidget_DragBoxPosition(
		const FVector2f& RelativeOffset, const TEnumAsByte<EHorizontalAlignment>& HAlign, const TEnumAsByte<EVerticalAlignment>& VAlign
		)
		: RelativeOffset(RelativeOffset)
		, HAlign(HAlign)
		, VAlign(VAlign)
	{}
};

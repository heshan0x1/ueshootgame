// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/SCompoundWidget.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

namespace UE::ToolWidgets
{
class SDraggableBoxOverlay;
	
/**
 * A widget for the draggable box itself, which requires its parent to handle its positioning in
 * response to the drag.
 *
 * Users probably shouldn't use this class directly; rather, they should use SDraggableBoxOverlay,
 * which will put its contents into a draggable box and properly handle the dragging without the
 * user having to set it up.
 */
class SDraggableBox : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SDraggableBox)
		: _IsDraggable(true)
	{}
	SLATE_DEFAULT_SLOT(FArguments, Content)

	/** Whether this widget should be allowed to be dragged. */
	SLATE_ATTRIBUTE(bool, IsDraggable)
	
	/** Invoked when the user has finished dragging the widget to a new position. */
	SLATE_EVENT(FSimpleDelegate, OnUserDraggedToNewPosition)
SLATE_END_ARGS()

void Construct(const FArguments& InArgs, const TSharedRef<SDraggableBoxOverlay>& InDraggableOverlay);

	struct FDragInfo
	{
		EHorizontalAlignment OriginalHorizontalAlignment = HAlign_Left;
		EVerticalAlignment OriginalVerticalAlignment = VAlign_Bottom;
		FVector2f OriginalAlignmentOffset = FVector2f::ZeroVector;
		FVector2f OriginalMousePosition;
	};

	void OnDragUpdate(const FPointerEvent& InMouseEvent, const FDragInfo& InDragInfo, bool bInDropped);

	//~ Begin SWidget
	FReply OnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	FReply OnDragDetected(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	//~ End SWidget

protected:

	TWeakPtr<SDraggableBoxOverlay> DraggableOverlayWeak;
	TSharedPtr<SWidget> InnerWidget;

private:

	/** Whether this widget should be allowed to be dragged. */
	TAttribute<bool> IsDraggableAttr;

	/** The content you want to be able to drag around in the parent widget. */
	FSimpleDelegate OnUserDraggedToNewPositionDelegate;
};
}
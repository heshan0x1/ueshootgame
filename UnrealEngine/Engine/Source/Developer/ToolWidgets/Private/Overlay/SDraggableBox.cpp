// Copyright Epic Games, Inc. All Rights Reserved.

#include "Overlay/SDraggableBox.h"

#include "Framework/Application/SlateApplication.h"
#include "Overlay/SDraggableBoxOverlay.h"
#include "Input/DragAndDrop.h"
#include "Styling/SlateTypes.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"

namespace UE::ToolWidgets
{
namespace DraggableBoxDetail
{
/** A drag/drop operation used by SDraggableBox. */
class FDraggableBoxUIDragOperation : public FDragDropOperation
{
public:
	FDraggableBoxUIDragOperation(const TSharedRef<SDraggableBox> InDraggableBox,
		const SDraggableBox::FDragInfo& InDragInfo)
		: DraggableBoxWeak(InDraggableBox)
		, DragInfo(InDragInfo)
	{
	}

	virtual ~FDraggableBoxUIDragOperation() override = default;

	//~ Begin FDragDropOperation
	virtual void OnDragged(const FDragDropEvent& InDragDropEvent)
	{
		if (TSharedPtr<SDraggableBox> DraggableBox = DraggableBoxWeak.Pin())
		{
			DraggableBox->OnDragUpdate(InDragDropEvent, DragInfo, /* Dropped */ false);
		}
	}

	virtual void OnDrop(bool bDropWasHandled, const FPointerEvent& InMouseEvent)
	{
		if (TSharedPtr<SDraggableBox> DraggableBox = DraggableBoxWeak.Pin())
		{
			DraggableBox->OnDragUpdate(InMouseEvent, DragInfo, /* Dropped */ true);
		}
	}
	//~ End FDragDropOperation

protected:
	TWeakPtr<SDraggableBox> DraggableBoxWeak;
	SDraggableBox::FDragInfo DragInfo;
};
}

void SDraggableBox::Construct(const FArguments& InArgs, const TSharedRef<SDraggableBoxOverlay>& InDraggableOverlay)
{
	DraggableOverlayWeak = InDraggableOverlay;
	InnerWidget = InArgs._Content.Widget;
	IsDraggableAttr = InArgs._IsDraggable;
	OnUserDraggedToNewPositionDelegate = InArgs._OnUserDraggedToNewPosition;

	ChildSlot
	[
		InArgs._Content.Widget
	];
}

void SDraggableBox::OnDragUpdate(const FPointerEvent& InMouseEvent, const FDragInfo& InDragInfo, bool bInDropped)
{
	const TSharedPtr<SDraggableBoxOverlay> DraggableOverlay = DraggableOverlayWeak.Pin();
	if (!DraggableOverlay.IsValid() || !IsDraggableAttr.Get())
	{
		return;
	}

	const FGeometry& MyGeometry = DraggableOverlay->GetTickSpaceGeometry();
	const FVector2f MouseOffset = (InMouseEvent.GetScreenSpacePosition() - InDragInfo.OriginalMousePosition)
		* (MyGeometry.GetLocalSize() / MyGeometry.GetAbsoluteSize());

	FVector2f NewAlignmentOffset = InDragInfo.OriginalAlignmentOffset;
	switch (InDragInfo.OriginalHorizontalAlignment)
	{
	case EHorizontalAlignment::HAlign_Left:
		NewAlignmentOffset.X += MouseOffset.X;
		break;

	case EHorizontalAlignment::HAlign_Right:
		NewAlignmentOffset.X -= MouseOffset.X;
		break;

	default:
		// Do nothing
		break;
	}

	switch (InDragInfo.OriginalVerticalAlignment)
	{
	case EVerticalAlignment::VAlign_Top:
		NewAlignmentOffset.Y += MouseOffset.Y;
		break;

	case EVerticalAlignment::VAlign_Bottom:
		NewAlignmentOffset.Y -= MouseOffset.Y;
		break;

	default:
		// Do nothing
		break;
	}

	DraggableOverlay->SetBoxHorizontalAlignment(InDragInfo.OriginalHorizontalAlignment);
	DraggableOverlay->SetBoxVerticalAlignment(InDragInfo.OriginalVerticalAlignment);
	DraggableOverlay->SetBoxAlignmentOffset(NewAlignmentOffset);

	if (bInDropped)
	{
		OnUserDraggedToNewPositionDelegate.ExecuteIfBound();
	}
}

FReply SDraggableBox::OnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	return FReply::Handled().DetectDrag(SharedThis(this), EKeys::LeftMouseButton);
}

FReply SDraggableBox::OnDragDetected(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	const TSharedPtr<SDraggableBoxOverlay> DraggableOverlay = DraggableOverlayWeak.Pin();
	if (DraggableOverlay && IsDraggableAttr.Get())
	{
		const FDragInfo DragInfo
		{
			DraggableOverlay->GetBoxHorizontalAlignment(),
			DraggableOverlay->GetBoxVerticalAlignment(),
			DraggableOverlay->GetBoxAlignmentOffset(),
			InMouseEvent.GetScreenSpacePosition()
		};
		using namespace DraggableBoxDetail;
		const TSharedRef<FDraggableBoxUIDragOperation> DragDropOperation = MakeShared<FDraggableBoxUIDragOperation>(
			SharedThis(this), DragInfo
		);
		return FReply::Handled().BeginDragDrop(DragDropOperation);
	}
	return FReply::Unhandled();
}
}
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/Images/SImage.h"

#define UE_API METAHUMANIMAGEVIEWER_API

DECLARE_MULTICAST_DELEGATE_OneParam(FOnViewChanged, FBox2f);
DECLARE_MULTICAST_DELEGATE(FOnGeometryChanged);

class SMetaHumanImageViewer : public SImage
{
public:

	SLATE_BEGIN_ARGS(SMetaHumanImageViewer) {}

		/** Image resource */
		SLATE_ATTRIBUTE(const FSlateBrush*, Image)

		SLATE_ATTRIBUTE(TSharedPtr<class FUICommandList>, CommandList)

	SLATE_END_ARGS()

	UE_API void Construct(const FArguments& InArgs);

	UE_API FReply HandleMouseButtonDown(const FGeometry& InGeometry, const FVector2f& InLocalMouse, const FKey& InEffectingButton);
	UE_API FReply HandleMouseButtonUp(const FGeometry& InGeometry, const FVector2f& InLocalMouse, const FKey& InEffectingButton);
	UE_API FReply HandleMouseMove(const FGeometry& InGeometry, const FVector2f& InLocalMouse);
	UE_API FReply HandleMouseWheel(const FGeometry& InGeometry, const FVector2f& InLocalMouse, float InWheelDelta);

	UE_API virtual FReply OnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	UE_API virtual FReply OnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	UE_API virtual FReply OnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	UE_API virtual FReply OnMouseWheel(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	UE_API virtual FReply OnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	UE_API virtual bool SupportsKeyboardFocus() const override;

	UE_API virtual int32 OnPaint(const FPaintArgs& InArgs, const FGeometry& InAllottedGeometry,
		const FSlateRect& InWidgetClippingRect, FSlateWindowElementList& OutDrawElements,
		int32 InLayerId, const FWidgetStyle& InWidgetStyle, bool InParentEnabled) const override;

	FOnViewChanged OnViewChanged;

	UE_API void SetNonConstBrush(FSlateBrush* InBrush);
	UE_API virtual void ResetView();

	UE_API void SetDrawBlanking(bool bInDrawBlanking);

protected:

	TSharedPtr<class FUICommandList> CommandList;

	FBox2f UVOrig;
	FVector2f MouseOrig;
	bool bIsPanning = false;
	mutable FGeometry Geometry;

	FOnGeometryChanged OnGeometryChanged;
	FSlateBrush* NonConstBrush = nullptr;
	UE_API virtual void GeometryChanged();

	bool bDrawBlanking = true;
};

#undef UE_API

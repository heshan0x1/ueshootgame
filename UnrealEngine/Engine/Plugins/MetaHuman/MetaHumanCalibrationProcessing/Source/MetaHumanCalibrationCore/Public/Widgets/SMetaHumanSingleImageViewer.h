// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SMetaHumanImageViewer.h"

#include "Utils/MetaHumanCalibrationAreaSelection.h"

#include "Engine/Texture2D.h"

#define UE_API METAHUMANCALIBRATIONCORE_API

class SMetaHumanCalibrationSingleImageViewer : public SMetaHumanImageViewer
{
public:
	DECLARE_DELEGATE_FourParams(FOnAddOverlays, FBox2d, const FGeometry&, FSlateWindowElementList&, int32&);
	DECLARE_DELEGATE_ThreeParams(FOnImageClick, FVector2D, FBox2d, FVector2D);

	SLATE_BEGIN_ARGS(SMetaHumanCalibrationSingleImageViewer)
		{
		}

		SLATE_ARGUMENT(TArray<FString>, Images)
		SLATE_EVENT(FOnAddOverlays, OnAddOverlays)
		SLATE_EVENT(FOnImageClick, OnImageClick)
	SLATE_END_ARGS()

	UE_API void Construct(const FArguments& InArgs);

	UE_API void SetImages(TArray<FString> InImages);
	UE_API void ShowImage(int32 InImageIndex);

	UE_API FIntVector2 GetImageSize() const;
	UE_API int32 GetImageNum() const;
	UE_API FString GetImagePath(int32 InImageIndex) const;
	UE_API const TArray<FString>& GetImagePaths() const;

	DECLARE_DELEGATE_ThreeParams(FAreaSelectionEnded, FSlateRect, FBox2d, FVector2D);
	UE_API void StartSelecting(FAreaSelectionEnded InOnSelectionEnded);

	UE_API bool IsSelecting() const;

private:

	UE_API virtual FReply OnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	UE_API virtual FReply OnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	UE_API virtual FReply OnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

	// Paint callback
	UE_API virtual int32 OnPaint(const FPaintArgs& InArgs, const FGeometry& InAllottedGeometry,
								 const FSlateRect& InCullingRect, FSlateWindowElementList& OutDrawElements,
								 int32 InLayerId, const FWidgetStyle& InWidgetStyle, bool bInParentEnabled) const override;

	TArray<FString> Images;
	FOnAddOverlays OnAddOverlays;
	FOnImageClick OnImageClick;
	FSlateBrush CameraImageViewerBrush;
	TStrongObjectPtr<UTexture2D> CameraImageTexture;

	TOptional<FMetaHumanCalibrationAreaSelection> DraggingArea;
	FAreaSelectionEnded OnSelectionEnded;
};

#undef UE_API
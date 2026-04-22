// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SMetaHumanSingleImageViewer.h"

#include "Utils/MetaHumanCalibrationUtils.h"
#include "ImageUtils.h"

namespace UE::MetaHuman::Private
{

TOptional<FImage> GetImage(const FString& InFullImagePath)
{
	FImage Image;

	bool bSuccess = FImageUtils::LoadImage(*InFullImagePath, Image);
	if (!bSuccess)
	{
		return {};
	}
	
	// Converting to UE texture required format (BGRA8)
	if (Image.Format != ERawImageFormat::BGRA8)
	{
		Image.ChangeFormat(ERawImageFormat::BGRA8, EGammaSpace::sRGB);
	}

	return Image;
}

}

void SMetaHumanCalibrationSingleImageViewer::Construct(const FArguments& InArgs)
{
	SMetaHumanImageViewer::Construct(SMetaHumanImageViewer::FArguments());

	Images = InArgs._Images;
	OnAddOverlays = InArgs._OnAddOverlays;
	OnImageClick = InArgs._OnImageClick;

	SetImage(&CameraImageViewerBrush);
	SetNonConstBrush(&CameraImageViewerBrush);
	CameraImageViewerBrush.SetUVRegion(FBox2f{ FVector2f{ 0.0f, 0.0f }, FVector2f{ 1.0f, 1.0f } });
	OnViewChanged.AddLambda([&](FBox2f InUV)
							{
								CameraImageViewerBrush.SetUVRegion(InUV);
							});

	ShowImage(0);
}

void SMetaHumanCalibrationSingleImageViewer::SetImages(TArray<FString> InImages)
{
	Images = MoveTemp(InImages);

	ShowImage(0);
}

void SMetaHumanCalibrationSingleImageViewer::ShowImage(int32 InImageIndex)
{
	if (!Images.IsValidIndex(InImageIndex))
	{
		return;
	}

	if (CameraImageTexture)
	{
		CameraImageTexture->MarkAsGarbage();
		CameraImageTexture = nullptr;
	}

	TOptional<FImage> MaybeImage = UE::MetaHuman::Private::GetImage(Images[InImageIndex]);
	if (!MaybeImage.IsSet())
	{
		return;
	}

	CameraImageTexture.Reset(FImageUtils::CreateTexture2DFromImage(MaybeImage.GetValue()));

	CameraImageViewerBrush.SetResourceObject(CameraImageTexture.Get());
	CameraImageViewerBrush.SetImageSize(FVector2D(CameraImageTexture->GetSizeX(), CameraImageTexture->GetSizeY()));
	CameraImageViewerBrush.DrawAs = ESlateBrushDrawType::Image;

	ResetView();
}

FIntVector2 SMetaHumanCalibrationSingleImageViewer::GetImageSize() const
{
	if (CameraImageTexture)
	{
		return { CameraImageTexture->GetSizeX(), CameraImageTexture->GetSizeY() };
	}

	return FIntVector2();
}

int32 SMetaHumanCalibrationSingleImageViewer::GetImageNum() const
{
	return Images.Num();
}

FString SMetaHumanCalibrationSingleImageViewer::GetImagePath(int32 InImageIndex) const
{
	if (Images.IsValidIndex(InImageIndex))
	{
		return Images[InImageIndex];
	}

	return FString();
}

const TArray<FString>& SMetaHumanCalibrationSingleImageViewer::GetImagePaths() const
{
	return Images;
}

void SMetaHumanCalibrationSingleImageViewer::StartSelecting(FAreaSelectionEnded InOnSelectionEnded)
{
	OnSelectionEnded = MoveTemp(InOnSelectionEnded);
}

bool SMetaHumanCalibrationSingleImageViewer::IsSelecting() const
{
	return OnSelectionEnded.IsBound();
}

FReply SMetaHumanCalibrationSingleImageViewer::OnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (!OnSelectionEnded.IsBound())
	{
		return SMetaHumanImageViewer::OnMouseButtonDown(InGeometry, InMouseEvent);
	}

	if (InMouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		OnSelectionEnded = nullptr;
		return SMetaHumanImageViewer::OnMouseButtonDown(InGeometry, InMouseEvent);
	}

	DraggingArea = FMetaHumanCalibrationAreaSelection(InGeometry.AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition()), EKeys::LeftMouseButton);

	FReply Reply = FReply::Handled();

	if (Reply.IsEventHandled())
	{
		Reply.CaptureMouse(SharedThis(this));
	}

	return Reply;
}

FReply SMetaHumanCalibrationSingleImageViewer::OnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (!OnSelectionEnded.IsBound())
	{
		return SMetaHumanImageViewer::OnMouseMove(InGeometry, InMouseEvent);
	}

	if (!DraggingArea.IsSet())
	{
		return SMetaHumanImageViewer::OnMouseMove(InGeometry, InMouseEvent);
	}

	FVector2D LocalMouse = InGeometry.AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition());

	if (!DraggingArea->IsDragging() && DraggingArea->AttemptDragStart(InMouseEvent))
	{
		DraggingArea->OnStart(LocalMouse);
	}

	DraggingArea->OnUpdate(LocalMouse);

	return FReply::Handled();
}

FReply SMetaHumanCalibrationSingleImageViewer::OnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (!OnSelectionEnded.IsBound())
	{
		if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
		{
			FVector2D LocalMouse = InGeometry.AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition());
			FBox2d UvRegion = CameraImageViewerBrush.GetUVRegion();
			FVector2D WidgetSize = InGeometry.GetLocalSize();

			OnImageClick.ExecuteIfBound(MoveTemp(LocalMouse), MoveTemp(UvRegion), MoveTemp(WidgetSize));
			return FReply::Handled();
		}

		return SMetaHumanImageViewer::OnMouseButtonUp(InGeometry, InMouseEvent);
	}

	if (InMouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return SMetaHumanImageViewer::OnMouseButtonUp(InGeometry, InMouseEvent);
	}

	ON_SCOPE_EXIT
	{
		OnSelectionEnded = nullptr;
	};

	if (!DraggingArea.IsSet())
	{
		return SMetaHumanImageViewer::OnMouseButtonUp(InGeometry, InMouseEvent);
	}

	ON_SCOPE_EXIT
	{
		DraggingArea.Reset();
	};

	if (!DraggingArea->IsDragging())
	{
		return SMetaHumanImageViewer::OnMouseButtonUp(InGeometry, InMouseEvent);
	}

	FVector2D LocalMouse = InGeometry.AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition());

	FSlateRect AreaSelected = DraggingArea->OnEnd(LocalMouse);

	FBox2d UvRegion = CameraImageViewerBrush.GetUVRegion();
	FVector2D WidgetSize = InGeometry.GetLocalSize();

	check(OnSelectionEnded.IsBound());
	OnSelectionEnded.Execute(MoveTemp(AreaSelected), MoveTemp(UvRegion), MoveTemp(WidgetSize));

	FReply Reply = FReply::Handled();

	if (Reply.IsEventHandled())
	{
		Reply.ReleaseMouseCapture();
	}

	return Reply;
}

int32 SMetaHumanCalibrationSingleImageViewer::OnPaint(const FPaintArgs& InArgs, const FGeometry& InAllottedGeometry,
													  const FSlateRect& InCullingRect, FSlateWindowElementList& OutDrawElements,
													  int32 InLayerId, const FWidgetStyle& InWidgetStyle, bool bInParentEnabled) const
{
	int32 NewLayerId = SMetaHumanImageViewer::OnPaint(InArgs, InAllottedGeometry, InCullingRect, OutDrawElements, InLayerId, InWidgetStyle, bInParentEnabled);

	if (DraggingArea.IsSet() && DraggingArea->IsDragging())
	{
		++NewLayerId;
		DraggingArea->OnDraw(InAllottedGeometry, OutDrawElements, NewLayerId);
	}
	else
	{
		OnAddOverlays.ExecuteIfBound(CameraImageViewerBrush.GetUVRegion(), InAllottedGeometry, OutDrawElements, NewLayerId);
	}

	return NewLayerId;
}
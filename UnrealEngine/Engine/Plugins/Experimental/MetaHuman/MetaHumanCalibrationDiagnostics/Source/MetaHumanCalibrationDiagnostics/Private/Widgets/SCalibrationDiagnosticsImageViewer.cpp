// Copyright Epic Games, Inc. All Rights Reserved.

#include "SCalibrationDiagnosticsImageViewer.h"
#include "Utils/MetaHumanCalibrationUtils.h"

#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SSpacer.h"

#include "SEditorViewportToolBarMenu.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"

#include "PropertyEditorModule.h"
#include "Modules/ModuleManager.h"

#include "ImgMediaSource.h"

#include "ParseTakeUtils.h"

#include "Fonts/FontMeasure.h"

#include "Misc/ScopedSlowTask.h"

#define LOCTEXT_NAMESPACE "CalibrationDiagnosticsImageViewer"

namespace UE::MetaHuman::Private
{

static constexpr int32 ExpectedNumberOfCameras = 2;

}

void SCalibrationDiagnosticsImageViewer::Construct(const FArguments& InArgs)
{
	CaptureData = InArgs._FootageCaptureData;
	check(CaptureData.Get());

	Options = InArgs._Options;
	check(Options.Get());

	FeatureDetector = InArgs._FeatureDetector;
	check(FeatureDetector.Get());

	ImageViewers.Reserve(UE::MetaHuman::Private::ExpectedNumberOfCameras);

	bool bShouldInitialize = true;
	if (CaptureData->ImageSequences.Num() < UE::MetaHuman::Private::ExpectedNumberOfCameras ||
		!IsValid(CaptureData->ImageSequences[0]) ||
		!IsValid(CaptureData->ImageSequences[1]))
	{
		bShouldInitialize = false;
	}

	TSharedPtr<SHorizontalBox> ImageViewerBox = SNew(SHorizontalBox);

	Options->AreaOfInterestsForCameras.AddUninitialized(UE::MetaHuman::Private::ExpectedNumberOfCameras);

	if (bShouldInitialize)
	{
		FMetaHumanCalibrationDiagnosticsCommands::Register();

		for (int32 Index = 0; Index < UE::MetaHuman::Private::ExpectedNumberOfCameras; ++Index)
		{
			TArray<FString> ImagePaths = UE::MetaHuman::Image::GetImagePaths(CaptureData->ImageSequences[Index]);

			TSharedPtr<SMetaHumanCalibrationSingleImageViewer> CameraImageViewer =
				SNew(SMetaHumanCalibrationSingleImageViewer)
				.Images(MoveTemp(ImagePaths))
				.OnAddOverlays(this, &SCalibrationDiagnosticsImageViewer::OnAddOverlays, Index)
				.OnImageClick(this, &SCalibrationDiagnosticsImageViewer::OnImageClicked, Index);

			ImageViewers.Add(MoveTemp(CameraImageViewer));
			CameraToolkitCommands.Add(Index, MakeShared<FUICommandList>());

			ImageViewerBox->AddSlot()
				.FillWidth(0.5f)
				[
					SNew(SOverlay)
					+ SOverlay::Slot()
					[
						ImageViewers[Index].ToSharedRef()
					]
					+ SOverlay::Slot()
					.VAlign(VAlign_Top)
					.HAlign(HAlign_Center)
					[
						GetViewToolbarWidget(Index).ToSharedRef()
					]
				];

			FIntVector2 ImageSize = ImageViewers[Index]->GetImageSize();
			FSlateRect InitialArea(FVector2D::ZeroVector, FVector2D(ImageSize));

			Options->AreaOfInterestsForCameras[Index].SetFromSlateRect(InitialArea);
		}
	}
	
	RegisterCommandHandlers();

	TSharedPtr<SVerticalBox> MainWidget =
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		[
			SNew(SBox)
			.Padding(FMargin(4.f))
			[
				SNew(SBorder)
				.Padding(FMargin(0))
				.BorderImage(FAppStyle::GetBrush("Brushes.Recessed"))
				[
					SNew(SOverlay)
					+ SOverlay::Slot()
					[
						ImageViewerBox.ToSharedRef()
					]
					+ SOverlay::Slot()
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(FText::FromString(TEXT("Images are not found")))
						.Visibility(this, &SCalibrationDiagnosticsImageViewer::ImagesNotFoundVisibility)
					]
				]
			]
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 2.0f, 0.0f, 0.0f)
		.VAlign(VAlign_Center)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
			.ForegroundColor(FLinearColor::Gray)
			.Padding(2.0f)
			[
				SAssignNew(ScrubberSlider, SMetaHumanImageViewerScrubber)
				.Style(&FMetaHumanCalibrationStyle::Get().GetWidgetStyle<FSliderStyle>("MetaHumanCalibration.Scrubber"))
				.OnValueChanged(this, &SCalibrationDiagnosticsImageViewer::OnScrubberValueChanged)
				.AllowVisualization(true)
				.FrameRate(CaptureData->Metadata.FrameRate)
			]
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 4.0f, 0.0f, 2.0f)
		.VAlign(VAlign_Center)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.Padding(8.0f, 0.0f, 0.0f, 0.0f)
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("MeanError", "Mean Error:"))
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(8.0f, 0.0f, 0.0f, 0.0f)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(this, &SCalibrationDiagnosticsImageViewer::HandleMeanErrorTextBlock)
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(12.0f, 0.0f, 0.0f, 0.0f)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("RMSError", "RMS Error:"))
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(8.0f, 0.0f, 0.0f, 0.0f)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(this, &SCalibrationDiagnosticsImageViewer::HandleRMSErrorTextBlock)
				.ColorAndOpacity(this, &SCalibrationDiagnosticsImageViewer::HandleRMSErrorColor)
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.Padding(8.0f, 0.0f)
			.VAlign(VAlign_Center)
			[
				SNew(SSpacer)
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0.0f, 0.0f, 2.0f, 0.0f)
			[
				SNew(SButton)
				.OnClicked(this, &SCalibrationDiagnosticsImageViewer::OnDetectButtonClicked)
				.IsEnabled(this, &SCalibrationDiagnosticsImageViewer::IsDetectButtonEnabled)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(SImage)
						.Image(FMetaHumanCalibrationStyle::Get().GetBrush("MetaHumanCalibration.Diagnostics.Icon"))
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(4.0f, 0.0f, 0.0f, 0.0f)
					[
						SNew(STextBlock)
						.Text(this, &SCalibrationDiagnosticsImageViewer::HandleDetectButtonText)
					]
				]
			]
		];

	ChildSlot
		[
			MainWidget.ToSharedRef()
		];

	if (bShouldInitialize)
	{
		ScrubberSlider->SetMinAndMaxValues(0.0f, ImageViewers[0]->GetImageNum() - 1);

		TArray<FString> CameraNames;
		CameraNames.Add(CaptureData->ImageSequences[0].GetName());
		CameraNames.Add(CaptureData->ImageSequences[1].GetName());

		TArray<FIntVector2> Dimensions;
		Dimensions.Add(ImageViewers[0]->GetImageSize());
		Dimensions.Add(ImageViewers[1]->GetImageSize());

		static const FVector2D GridSize(4.0, 4.0);
		Calculator.Reset(new FMetaHumanCalibrationErrorCalculator(GridSize, CameraNames, Dimensions));
	}

	SetCanTick(true);

	SetImages(0);
}

void SCalibrationDiagnosticsImageViewer::SetImages(int32 InFrameId)
{
	CurrentFrameId = InFrameId;

	for (TSharedPtr<SMetaHumanCalibrationSingleImageViewer> ImageViewer : ImageViewers)
	{
		ImageViewer->ShowImage(CurrentFrameId);
	}
}

void SCalibrationDiagnosticsImageViewer::UpdateState()
{
	Calculator->Invalidate();
}

void SCalibrationDiagnosticsImageViewer::ResetState()
{
	DetectedFrames.Empty();
	ScrubberSlider->SetValue(0);
	Calculator->Invalidate();
	PendingValueChange = 0;
}

void SCalibrationDiagnosticsImageViewer::OnClose()
{
	FMetaHumanCalibrationDiagnosticsCommands::Unregister();
}

void SCalibrationDiagnosticsImageViewer::OnAddOverlays(FBox2d InUvRegion,
													   const FGeometry& InAllottedGeometry,
													   FSlateWindowElementList& OutDrawElements,
													   int32& OutLayerId,
													   int32 InCameraIndex)
{
	if (!ImageViewers.IsValidIndex(InCameraIndex))
	{
		return;
	}

	if (EnumHasAnyFlags(ShowViewOptions[InCameraIndex], EViewOptions::DetectedFeatures))
	{
		++OutLayerId;
		ShowDetectedFeatures(InUvRegion, InAllottedGeometry, OutDrawElements, OutLayerId, InCameraIndex);
	}

	if (EnumHasAnyFlags(ShowViewOptions[InCameraIndex], EViewOptions::AreaOfInterests))
	{
		++OutLayerId;
		ShowAreaOfInterest(InUvRegion, InAllottedGeometry, OutDrawElements, OutLayerId, InCameraIndex);
	}

	if (EnumHasAnyFlags(ShowViewOptions[InCameraIndex], EViewOptions::ErrorsPerBlock))
	{
		++OutLayerId;
		ShowGridMap(InUvRegion, InAllottedGeometry, OutDrawElements, OutLayerId, InCameraIndex);
	}
}

void SCalibrationDiagnosticsImageViewer::OnImageClicked(FVector2D InMousePoint, FBox2d InUvRegion, FVector2D InWidgetSize, int32 InCameraIndex)
{
	using namespace UE::MetaHuman::Points;

	if (!DetectedFrames.Contains(CurrentFrameId))
	{
		return;
	}

	const FVector2D ImageSize = FVector2D(ImageViewers[InCameraIndex]->GetImageSize());

	FVector2D MousePointOnImage = MapWidgetPointToTextureSpace(InMousePoint, InWidgetSize, InUvRegion, ImageSize);
	if (!FBox2d(FVector2D::ZeroVector, ImageSize).IsInside(MousePointOnImage))
	{
		return;
	}

	const FString& Name = CaptureData->ImageSequences[InCameraIndex]->GetName();

	TArray<int32> BlockIndices = Calculator->GetBlockIndices(Name);
	
	for (int32 BlockIndex : BlockIndices)
	{
		if (Calculator->GetBlock(Name, BlockIndex).IsInside(MousePointOnImage))
		{
			Calculator->ToggleMarkBlock(Name, BlockIndex);
			break;
		}
	}
}

void SCalibrationDiagnosticsImageViewer::ShowDetectedFeatures(FBox2d InUVRegion, const FGeometry& InAllottedGeometry,
															  FSlateWindowElementList& OutDrawElements, int32& OutLayerId, int32 InCameraIndex)
{
	using namespace UE::MetaHuman::Points;

	if (!DetectedFrames.Contains(CurrentFrameId))
	{
		return;
	}

	FDetectedFeatures Features = FeatureDetector->GetDetectedFeatures(CurrentFrameId);

	if (!Features.IsValid())
	{
		return;
	}

	const FVector2D WidgetSize = InAllottedGeometry.GetLocalSize();
	const FIntVector2 ImageSize = ImageViewers[InCameraIndex]->GetImageSize();

	const FVector2D ImageSizeVector(ImageSize.X, ImageSize.Y);

	if (ImageSizeVector.IsZero())
	{
		return;
	}

	const TArray<FVector2D>& CameraPoints = Features.CameraPoints[InCameraIndex].Points;
	const TArray<FVector2D>& ReprojectedPoints = Features.Points3dReprojected[InCameraIndex].Points;

	check(CameraPoints.Num() == ReprojectedPoints.Num());

	for (int32 PointIndex = 0; PointIndex < CameraPoints.Num(); ++PointIndex)
	{
		FVector2D ScaledCameraPoint = MapTexturePointToLocalWidgetSpace(CameraPoints[PointIndex], ImageSizeVector, InUVRegion, WidgetSize);
		FVector2D ScaledReprojectedPoint = MapTexturePointToLocalWidgetSpace(ReprojectedPoints[PointIndex], ImageSizeVector, InUVRegion, WidgetSize);

		double Scale = ScaledCameraPoint.X / CameraPoints[PointIndex].X;

		if (IsOutsideWidgetBounds(ScaledCameraPoint, WidgetSize) || IsOutsideWidgetBounds(ScaledReprojectedPoint, WidgetSize))
		{
			continue;
		}

		FSlateRect AreaOfInterest = Options->AreaOfInterestsForCameras[InCameraIndex].GetSlateRect();

		if (!AreaOfInterest.ContainsPoint(CameraPoints[PointIndex]) ||
			!AreaOfInterest.ContainsPoint(ReprojectedPoints[PointIndex]))
		{
			continue;
		}

		FVector2D PointDiff = CameraPoints[PointIndex] - ReprojectedPoints[PointIndex];
		double Error = FMath::Sqrt(PointDiff.X * PointDiff.X + PointDiff.Y * PointDiff.Y);

		static constexpr float MinThickness = 1.0;
		static constexpr float MaxThickness = 3.0;
		static constexpr float BaselineThickness = 1.0;
		float Thickness = FMath::Clamp(BaselineThickness / InUVRegion.GetSize().X, MinThickness, MaxThickness);

		float Normalized = FMath::Clamp(FMath::GetRangePct(0.0, Options->ReprojectionErrorThreshold, Error), 0.0, 1.0);

		FLinearColor HeatColor = FLinearColor::LerpUsingHSV(FLinearColor::Green, FLinearColor::Red, Normalized);

		TArray<FVector2D> LinePoints = { ScaledCameraPoint, ScaledReprojectedPoint };
		FSlateDrawElement::MakeLines(
			OutDrawElements,
			OutLayerId,
			InAllottedGeometry.ToPaintGeometry(),
			LinePoints,
			ESlateDrawEffect::None,
			HeatColor,
			true,
			Thickness
		);
	}
}

void SCalibrationDiagnosticsImageViewer::ShowAreaOfInterest(FBox2d InUVRegion, const FGeometry& InAllottedGeometry,
													   FSlateWindowElementList& OutDrawElements, int32& OutLayerId, int32 InCameraIndex)
{
	using namespace UE::MetaHuman::Points;

	const FVector2D WidgetSize = InAllottedGeometry.GetLocalSize();
	const FIntVector2 ImageSize = ImageViewers[InCameraIndex]->GetImageSize();

	const FVector2D ImageSizeVector(ImageSize.X, ImageSize.Y);

	if (ImageSizeVector.IsZero())
	{
		return;
	}

	FSlateRect AreaOfInterest = Options->AreaOfInterestsForCameras[InCameraIndex].GetSlateRect();

	// No need to show the area of interest if the area is the full image
	if (AreaOfInterest.GetTopLeft() == FVector2D::ZeroVector &&
		AreaOfInterest.GetBottomRight() == ImageSizeVector)
	{
		return;
	}

	FVector2D ActualCellSize = MapTextureSizeToLocalWidgetSize(AreaOfInterest.GetTopLeft(), AreaOfInterest.GetSize(), ImageSizeVector, InUVRegion, WidgetSize);
	FVector2D TopLeft = MapTexturePointToLocalWidgetSpace(AreaOfInterest.GetTopLeft(), ImageSizeVector, InUVRegion, WidgetSize);

	TopLeft = FVector2D::Clamp(TopLeft, FVector2D::ZeroVector, WidgetSize);

	FSlateDrawElement::MakeBox(
		OutDrawElements,
		OutLayerId,
		InAllottedGeometry.ToPaintGeometry(ActualCellSize, FSlateLayoutTransform(TopLeft)),
		FAppStyle::GetBrush(TEXT("MarqueeSelection")),
		ESlateDrawEffect::NoPixelSnapping
	);
}

void SCalibrationDiagnosticsImageViewer::ShowGridMap(FBox2d InUVRegion, const FGeometry& InAllottedGeometry,
													 FSlateWindowElementList& OutDrawElements, int32& OutLayerId, int32 InCameraIndex)
{
	using namespace UE::MetaHuman::Points;

	const FVector2D WidgetSize = InAllottedGeometry.GetLocalSize();
	const FVector2D ImageSize = FVector2D(ImageViewers[InCameraIndex]->GetImageSize());

	const FString& Name = CaptureData->ImageSequences[InCameraIndex]->GetName();

	TArray<int32> BlockIndices = Calculator->GetBlockIndices(Name);

	for (int32 BlockIndex : BlockIndices)
	{
		const FBox2D Cell = Calculator->GetBlock(Name, BlockIndex);

		if (!Calculator->ContainsErrors(CurrentFrameId))
		{
			continue;
		}

		FMetaHumanCalibrationErrorCalculator::FErrors Errors = 
			Calculator->GetErrorsForBlock(Name, BlockIndex, CurrentFrameId);

		static FNumberFormattingOptions NumberFormattingOptions;
		NumberFormattingOptions.MaximumFractionalDigits = 1;
		NumberFormattingOptions.MinimumFractionalDigits = 1;

		if (Errors.Errors.IsEmpty())
		{
			continue;
		}

		FLinearColor Color = HandleErrorColor(Errors.RMSError, Options->RMSErrorThreshold).GetSpecifiedColor();
		Color.A = 0.2f;

		FVector2D ActualCellSize =
			MapTextureSizeToLocalWidgetSize(Cell.Min, Cell.GetSize(), ImageSize, InUVRegion, WidgetSize);

		FVector2D BeginCellPoint =
			MapTexturePointToLocalWidgetSpace(Cell.Min, ImageSize, InUVRegion, WidgetSize);

		FVector2D RealCellSize =
			MapRealTextureSizeToLocalWidgetSize(BeginCellPoint, Cell.GetSize(), ImageSize, InUVRegion, WidgetSize);

		FSlateDrawElement::MakeBox(
			OutDrawElements,
			OutLayerId,
			InAllottedGeometry.ToPaintGeometry(ActualCellSize, FSlateLayoutTransform(FVector2D::Clamp(BeginCellPoint, FVector2D(0.0, 0.0), WidgetSize))),
			FCoreStyle::Get().GetBrush("WhiteBrush"),
			ESlateDrawEffect::NoPixelSnapping,
			Color
		);

		FTextBuilder TextBuilder;
		TextBuilder.AppendLineFormat(FText::FromString(TEXT("RMS {0}px")), FText::AsNumber(Errors.RMSError, &NumberFormattingOptions));

		if (Calculator->IsBlockMarked(Name, BlockIndex))
		{
			TextBuilder.AppendLineFormat(FText::FromString(TEXT("Mean {0}px")), 
										 FText::AsNumber(Errors.MeanError, &NumberFormattingOptions));
			TextBuilder.AppendLineFormat(FText::FromString(TEXT("Median {0}px")),
										 FText::AsNumber(Errors.MedianError, &NumberFormattingOptions));
			TextBuilder.AppendLineFormat(FText::FromString(TEXT("Count {0}")), 
										 FText::AsNumber(Errors.Errors.Num(), &NumberFormattingOptions));
		}

		float Size = 6;
		FFontOutlineSettings FontSettings(0, FLinearColor::White);
		FSlateFontInfo FontInfo = FCoreStyle::GetDefaultFontStyle("Regular", Size, FontSettings);

		FVector2D FontLocation = BeginCellPoint + (RealCellSize / 2);

		const TSharedRef<FSlateFontMeasure> FontMeasure =
			FSlateApplication::Get().GetRenderer()->GetFontMeasureService();

		const FText Text = TextBuilder.ToText();
		FVector2D FontSize = FontMeasure->Measure(Text, FontInfo);

		FontLocation.X -= FontSize.X / 2;
		FontLocation.Y -= FontSize.Y / 2;

		if (!IsOutsideWidgetBounds(FontLocation, WidgetSize) &&
			!IsOutsideWidgetBounds(FontLocation + FontSize, WidgetSize))
		{
			FSlateDrawElement::MakeText(OutDrawElements,
										OutLayerId,
										InAllottedGeometry.ToPaintGeometry(InAllottedGeometry.Size, FSlateLayoutTransform(FontLocation)),
										Text,
										FontInfo,
										ESlateDrawEffect::NoPixelSnapping,
										FLinearColor::White);
		}
	}
}

EVisibility SCalibrationDiagnosticsImageViewer::ImagesNotFoundVisibility() const
{
	bool bShouldPresentText = true;
	for (const TSharedPtr<SMetaHumanCalibrationSingleImageViewer>& ImageViewer : ImageViewers)
	{
		bShouldPresentText &= (ImageViewer->GetImageNum() == 0);
	}

	return bShouldPresentText ? EVisibility::Visible : EVisibility::Collapsed;
}

FText SCalibrationDiagnosticsImageViewer::HandleDetectButtonText() const
{
	if (DetectedFrames.Contains(CurrentFrameId))
	{
		return LOCTEXT("SimpleButtonDetected", "Features Detected");
	}

	return LOCTEXT("SimpleButtonDetect", "Detect Features");
}

FReply SCalibrationDiagnosticsImageViewer::OnDetectButtonClicked()
{
	if (DetectedFrames.Contains(CurrentFrameId))
	{
		return FReply::Handled();
	}

	DetectForFrame(CurrentFrameId);

	return FReply::Handled();
}

bool SCalibrationDiagnosticsImageViewer::DetectForFrame(int32 InFrame)
{
	FDetectedFeatures Features = FeatureDetector->DetectFeatures(InFrame);
	if (!Features.IsValid())
	{
		return false;
	}

	CalculateErrors(InFrame);
	ScrubberSlider->SetFrameState(InFrame, EFrameState::Neutral);
	DetectedFrames.Add(InFrame);

	return true;
}

bool SCalibrationDiagnosticsImageViewer::IsDetectButtonEnabled() const
{
	return !DetectedFrames.Contains(CurrentFrameId);
}

FText SCalibrationDiagnosticsImageViewer::HandleMeanErrorTextBlock() const
{
	double TotalMeanError = 0.0;
	if (CurrentFrameMean.IsSet())
	{
		TotalMeanError = CurrentFrameMean.GetValue();
	}

	return HandleErrorTextBlock(TotalMeanError);
}

FText SCalibrationDiagnosticsImageViewer::HandleRMSErrorTextBlock() const
{
	double TotalRMSError = 0.0;
	if (CurrentFrameRMS.IsSet())
	{
		TotalRMSError = CurrentFrameRMS.GetValue();
	}
	
	return HandleErrorTextBlock(TotalRMSError);
}

FSlateColor SCalibrationDiagnosticsImageViewer::HandleRMSErrorColor() const
{
	double TotalRMSError = 0.0;
	if (CurrentFrameRMS.IsSet())
	{
		TotalRMSError = CurrentFrameRMS.GetValue();
	}

	return HandleErrorColor(TotalRMSError, Options->RMSErrorThreshold);
}

FText SCalibrationDiagnosticsImageViewer::HandleErrorTextBlock(double InError) const
{
	FNumberFormattingOptions FormattingOptions;
	FormattingOptions.MaximumFractionalDigits = 2;
	FormattingOptions.MinimumFractionalDigits = 2;

	return FText::AsNumber(InError, &FormattingOptions);
}

FSlateColor SCalibrationDiagnosticsImageViewer::HandleErrorColor(double InError, double InThreshold) const
{
	float Normalized = FMath::Clamp(FMath::GetRangePct(0.0, InThreshold, InError), 0.0, 1.0);

	FLinearColor HeatColor = FLinearColor::LerpUsingHSV(FLinearColor::Green, FLinearColor::Red, Normalized);

	return FSlateColor(HeatColor);
}

void SCalibrationDiagnosticsImageViewer::UpdateErrors()
{
	for (int32 FrameIndex : DetectedFrames)
	{
		CalculateErrors(FrameIndex);
	}
}

void SCalibrationDiagnosticsImageViewer::CalculateErrors(int32 InFrameIndex, bool bInUpdateUI)
{
	FDetectedFeatures Features = FeatureDetector->GetDetectedFeatures(InFrameIndex);

	if (!Features.IsValid())
	{
		return;
	}

	Calculator->Update(Features);

	if (bInUpdateUI)
	{
		CurrentFrameRMS = Calculator->GetRMSErrorForFrame(InFrameIndex);
		CurrentFrameMean = Calculator->GetMeanErrorForFrame(InFrameIndex);
	}
}

void SCalibrationDiagnosticsImageViewer::Tick(const FGeometry& InAllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	if (PendingValueChange.IsSet())
	{
		SetImages(PendingValueChange.GetValue());
		PendingValueChange.Reset();

		if (Calculator && Calculator->ContainsErrors(CurrentFrameId))
		{
			CurrentFrameRMS = Calculator->GetRMSErrorForFrame(CurrentFrameId);
			CurrentFrameMean = Calculator->GetMeanErrorForFrame(CurrentFrameId);
		}
		else
		{
			CurrentFrameRMS.Reset();
			CurrentFrameMean.Reset();
		}
	}

	if (Calculator && !Calculator->ContainsErrors())
	{
		UpdateErrors();
	}
}

void SCalibrationDiagnosticsImageViewer::OnScrubberValueChanged(float InValue)
{
	int32 Value = FMath::Floor(InValue);
	PendingValueChange = Value;
}

TSharedPtr<SWidget> SCalibrationDiagnosticsImageViewer::GetViewToolbarWidget(int32 InCameraIndex)
{
	check(CaptureData->ImageSequences.IsValidIndex(InCameraIndex));

	FSlimHorizontalToolBarBuilder ToolbarBuilder(CameraToolkitCommands[InCameraIndex], FMultiBoxCustomization::None);
	ToolbarBuilder.SetStyle(&FAppStyle::Get(), TEXT("EditorViewportToolBar"));
	ToolbarBuilder.SetLabelVisibility(EVisibility::Visible);

	const FText ViewTextName = InCameraIndex == 0 ? LOCTEXT("ViewA", "A") : LOCTEXT("ViewB", "B");
	const FName ViewName = *ViewTextName.ToString();

	const FMetaHumanCalibrationDiagnosticsCommands& Commands = FMetaHumanCalibrationDiagnosticsCommands::Get();

	ToolbarBuilder.BeginSection(ViewName);
	{
		ToolbarBuilder.AddToolBarButton(Commands.SelectAreaOfInterest,
										NAME_None,
										FText(),
										Commands.SelectAreaOfInterest->GetDescription(),
										FSlateIcon(FAppStyle::GetAppStyleSetName(), "EditorViewport.SelectMode"));

		TSharedRef<SEditorViewportToolbarMenu> ViewDisplayOptionsDropdownMenu = SNew(SEditorViewportToolbarMenu)
			.ToolTipText(FText::Format(LOCTEXT("CalibDiagViewDisplayOptionsMenuToolTip", "Display Options for View {0}"), { ViewTextName }))
			.Label(FText::Format(LOCTEXT("CalibDiagViewDisplayOptionsMenu", "{0}"), { ViewTextName }))
			.OnGetMenuContent(this, &SCalibrationDiagnosticsImageViewer::FillDisplayOptionsForViewMenu, InCameraIndex);

		TSharedPtr<FUICommandList> CommandList = CameraToolkitCommands[InCameraIndex];
		ToolbarBuilder.AddWidget(ViewDisplayOptionsDropdownMenu);
	}
	ToolbarBuilder.EndSection();

	return ToolbarBuilder.MakeWidget();
}

TSharedRef<SWidget> SCalibrationDiagnosticsImageViewer::FillDisplayOptionsForViewMenu(int32 InCameraIndex)
{
	const bool bShouldCloseWindowAfterMenuSelection = true;
	TSharedPtr<FUICommandList> CommandList = CameraToolkitCommands[InCameraIndex];
	FMenuBuilder MenuBuilder{ bShouldCloseWindowAfterMenuSelection, CommandList };

	const FMetaHumanCalibrationDiagnosticsCommands& Commands = FMetaHumanCalibrationDiagnosticsCommands::Get();

	MenuBuilder.BeginSection(TEXT("ViewControlExtensionSection"), LOCTEXT("Diag_ViewControlExtensionSectionLabel", "Control"));
	{
		MenuBuilder.AddMenuEntry(Commands.ResetView);
	}
	MenuBuilder.EndSection();

	MenuBuilder.BeginSection(TEXT("DiagnosticsExtensionSection"), LOCTEXT("Diag_DiagnosticsExtensionSectionLabel", "Diagnostics"));
	{
		MenuBuilder.AddMenuEntry(Commands.TogglePoints);
		MenuBuilder.AddMenuEntry(Commands.TogglePerBlockErrors);
		MenuBuilder.AddMenuEntry(Commands.ToggleAreaOfInterest);
	}
	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}

void SCalibrationDiagnosticsImageViewer::RegisterCommandHandlers()
{
	if (CameraToolkitCommands.IsEmpty())
	{
		return;
	}

	FMetaHumanCalibrationDiagnosticsCommands& Commands = FMetaHumanCalibrationDiagnosticsCommands::Get();

	for (int32 Index = 0; Index < UE::MetaHuman::Private::ExpectedNumberOfCameras; ++Index)
	{
		CameraToolkitCommands[Index]->MapAction(Commands.SelectAreaOfInterest,
												FExecuteAction::CreateSP(this, &SCalibrationDiagnosticsImageViewer::StartAreaOfInterestSelection, Index));

		CameraToolkitCommands[Index]->MapAction(Commands.ResetView,
												FExecuteAction::CreateSP(this, &SCalibrationDiagnosticsImageViewer::ResetView, Index));

		CameraToolkitCommands[Index]->MapAction(Commands.TogglePoints,
												FExecuteAction::CreateSP(this, &SCalibrationDiagnosticsImageViewer::ToggleView, Index, EViewOptions::DetectedFeatures),
												nullptr,
												FGetActionCheckState::CreateSP(this, &SCalibrationDiagnosticsImageViewer::IsViewToggled, Index, EViewOptions::DetectedFeatures));

		CameraToolkitCommands[Index]->MapAction(Commands.TogglePerBlockErrors,
												FExecuteAction::CreateSP(this, &SCalibrationDiagnosticsImageViewer::ToggleView, Index, EViewOptions::ErrorsPerBlock),
												nullptr,
												FGetActionCheckState::CreateSP(this, &SCalibrationDiagnosticsImageViewer::IsViewToggled, Index, EViewOptions::ErrorsPerBlock));

		CameraToolkitCommands[Index]->MapAction(Commands.ToggleAreaOfInterest,
												FExecuteAction::CreateSP(this, &SCalibrationDiagnosticsImageViewer::ToggleView, Index, EViewOptions::AreaOfInterests),
												nullptr,
												FGetActionCheckState::CreateSP(this, &SCalibrationDiagnosticsImageViewer::IsViewToggled, Index, EViewOptions::AreaOfInterests));

		ShowViewOptions.Add(Index, EViewOptions::All);
	}
}

void SCalibrationDiagnosticsImageViewer::StartAreaOfInterestSelection(int32 InCameraIndex)
{
	check(ImageViewers.IsValidIndex(InCameraIndex));

	SMetaHumanCalibrationSingleImageViewer::FAreaSelectionEnded SelectionEnded =
		SMetaHumanCalibrationSingleImageViewer::FAreaSelectionEnded::CreateSP(this, &SCalibrationDiagnosticsImageViewer::UpdateAfterSelection, InCameraIndex);
	ImageViewers[InCameraIndex]->StartSelecting(MoveTemp(SelectionEnded));
}

void SCalibrationDiagnosticsImageViewer::UpdateAfterSelection(FSlateRect InSlateRect, FBox2d InUvRegion, FVector2D InWidgetSize, int32 InCameraIndex)
{
	using namespace UE::MetaHuman::Points;

	check(Options->AreaOfInterestsForCameras.IsValidIndex(InCameraIndex));
	check(ImageViewers.IsValidIndex(InCameraIndex));

	FVector2D TextureSize = FVector2D(ImageViewers[InCameraIndex]->GetImageSize());

	FVector2D TopLeft = MapWidgetPointToTextureSpace(InSlateRect.GetTopLeft(), InWidgetSize, InUvRegion, TextureSize);
	FVector2D BottomRight = MapWidgetPointToTextureSpace(InSlateRect.GetBottomRight(), InWidgetSize, InUvRegion, TextureSize);

	TopLeft = FVector2D::Clamp(TopLeft, FVector2D::ZeroVector, TextureSize);
	BottomRight = FVector2D::Clamp(BottomRight, FVector2D::ZeroVector, TextureSize);

	Options->AreaOfInterestsForCameras[InCameraIndex].TopLeft = MoveTemp(TopLeft);
	Options->AreaOfInterestsForCameras[InCameraIndex].BottomRight = MoveTemp(BottomRight);

	Calculator->SetAreaOfInterestForCamera(CaptureData->ImageSequences[InCameraIndex].GetName(), 
										   Options->AreaOfInterestsForCameras[InCameraIndex].GetBox2D());
	Calculator->Invalidate();
}

void SCalibrationDiagnosticsImageViewer::ResetView(int32 InCameraIndex)
{
	check(ImageViewers.IsValidIndex(InCameraIndex));
	ImageViewers[InCameraIndex]->ResetView();
}

void SCalibrationDiagnosticsImageViewer::ToggleView(int32 InIndex, EViewOptions InView)
{
	if (EnumHasAnyFlags(ShowViewOptions[InIndex], InView))
	{
		EnumRemoveFlags(ShowViewOptions[InIndex], InView);
	}
	else
	{
		EnumAddFlags(ShowViewOptions[InIndex], InView);
	}
}

ECheckBoxState SCalibrationDiagnosticsImageViewer::IsViewToggled(int32 InIndex, EViewOptions InView) const
{
	return EnumHasAnyFlags(ShowViewOptions[InIndex], InView) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

FReply SCalibrationDiagnosticsImageViewer::OnKeyUp(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	bool bIsHandled = true;
	for (int32 CameraIndex = 0; CameraIndex < ImageViewers.Num(); ++CameraIndex)
	{
		bIsHandled = bIsHandled && CameraToolkitCommands[CameraIndex]->ProcessCommandBindings(InKeyEvent);
	}

	if (bIsHandled)
	{
		return FReply::Handled();
	}

	return SWidget::OnKeyUp(MyGeometry, InKeyEvent);
}

#undef LOCTEXT_NAMESPACE
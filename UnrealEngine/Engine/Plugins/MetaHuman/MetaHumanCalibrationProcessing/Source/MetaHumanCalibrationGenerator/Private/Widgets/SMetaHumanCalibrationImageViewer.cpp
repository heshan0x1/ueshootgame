// Copyright Epic Games, Inc. All Rights Reserved.

#include "SMetaHumanCalibrationImageViewer.h"

#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"

#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SSpacer.h"
#include "SEditorViewportToolBarMenu.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"

#include "Fonts/FontMeasure.h"

#include "ImgMediaSource.h"
#include "ImageSequenceUtils.h"

#include "Framework/Application/SlateApplication.h"

#include "ParseTakeUtils.h"

#include "Templates/Greater.h"
#include "Settings/MetaHumanCalibrationGeneratorSettings.h"
#include "Utils/MetaHumanCalibrationUtils.h"
#include "Utils/MetaHumanCalibrationFrameResolver.h"

#include "Styling/StyleColors.h"

#define LOCTEXT_NAMESPACE "MetaHumanCalibrationImageViewer"

namespace UE::MetaHuman::Private
{

static constexpr int32 ExpectedNumberOfCameras = 2;

}

void SMetaHumanCalibrationImageViewer::Construct(const FArguments& InArgs, const TSharedPtr<SWindow>& OwningWindow, const TSharedRef<class SDockTab>& OwningTab)
{
	using namespace UE::MetaHuman;

	FrameSelected = InArgs._FrameSelected;

	State = InArgs._State;
	check(State.Pin());
	DetectForFrame = InArgs._DetectForFrame;

	check(InArgs._FootageCaptureData);
	CaptureData = InArgs._FootageCaptureData;

	FMetaHumanCalibrationViewCommands::Register();

	TSharedPtr<FMetaHumanCalibrationGeneratorState> SharedState = State.Pin();
	SharedState->Options->AreaOfInterestsForCameras.AddUninitialized(Private::ExpectedNumberOfCameras);

	TOptional<FMetaHumanCalibrationFrameResolver> ResolverOpt = FMetaHumanCalibrationFrameResolver::CreateFromCaptureData(CaptureData.Get());

	TSharedPtr<SHorizontalBox> ImageViewerBox = SNew(SHorizontalBox);
	if (ResolverOpt.IsSet())
	{
		FMetaHumanCalibrationFrameResolver Resolver = MoveTemp(ResolverOpt.GetValue());

		for (int32 Index = 0; Index < Private::ExpectedNumberOfCameras; ++Index)
		{
			TArray<FString> CameraImagePaths;
			Resolver.GetFramePathsForCameraIndex(Index, CameraImagePaths);

			TSharedPtr<SMetaHumanCalibrationSingleImageViewer> CameraImageViewer =
				SNew(SMetaHumanCalibrationSingleImageViewer)
				.Images(MoveTemp(CameraImagePaths))
				.OnAddOverlays(this, &SMetaHumanCalibrationImageViewer::AddOverlay, Index);

			SingleImageViewers.Add(MoveTemp(CameraImageViewer));
			CameraToolkitCommands.Add(Index, MakeShared<FUICommandList>());

			ImageViewerBox->AddSlot()
				.FillWidth(0.5f)
				[
					SNew(SOverlay)
						+ SOverlay::Slot()
						[
							SingleImageViewers[Index].ToSharedRef()
						]
						+ SOverlay::Slot()
						.VAlign(VAlign_Top)
						.HAlign(HAlign_Center)
						[
							GetViewToolbarWidget(Index).ToSharedRef()
						]
				];

			FIntVector2 ImageSize = SingleImageViewers[Index]->GetImageSize();
			FBox2D InitialArea(FVector2D::ZeroVector, FVector2D(ImageSize));

			SharedState->Options->AreaOfInterestsForCameras[Index].SetFromBox2D(InitialArea);
		}
	}

	double FrameRate = FMath::IsNearlyZero(CaptureData->Metadata.FrameRate) ? 60.0 : CaptureData->Metadata.FrameRate;

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
					ImageViewerBox.ToSharedRef()
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
				.OnValueChanged(this, &SMetaHumanCalibrationImageViewer::OnScrubberValueChanged)
				.AllowVisualization(true)
				.FrameRate(FrameRate)
			]
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 4.0f, 0.0f, 2.0f)
		.VAlign(VAlign_Center)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
			.BorderBackgroundColor(FSlateColor(EStyleColor::ForegroundHeader))
			.Padding(0.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.Padding(2.0f)
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "DetailsView.NameAreaButton")
					.Text(LOCTEXT("SelectFrame", "Select"))
					.HAlign(HAlign_Center)
					.OnClicked(this, &SMetaHumanCalibrationImageViewer::OnFrameSelectedClicked)
					[
						SNew(SImage)
						.Image(this, &SMetaHumanCalibrationImageViewer::GetSelectFrameButtonImage)
					]
				]
				+ SHorizontalBox::Slot()
				.Padding(2.0f)
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "DetailsView.NameAreaButton")
					.Text(LOCTEXT("PreviousSelectedFrame", "Previous"))
					.ToolTipText(LOCTEXT("PreviousSelectedFrame_Tooltip", "Jumps to the previous selected frame"))
					.HAlign(HAlign_Center)
					.OnClicked(this, &SMetaHumanCalibrationImageViewer::OnPreviousSelectedFrameClicked)
					.IsEnabled(this, &SMetaHumanCalibrationImageViewer::IsPreviousNextButtonEnabled)
					[
						SNew(SImage)
						.Image(FAppStyle::GetBrush("Animation.Backward_End"))
					]
				]
				+ SHorizontalBox::Slot()
				.Padding(2.0f)
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "DetailsView.NameAreaButton")
					.Text(LOCTEXT("NextSelectedFrame", "Next"))
					.ToolTipText(LOCTEXT("NextSelectedFrame_Tooltip", "Jumps to the next selected frame"))
					.HAlign(HAlign_Center)
					.OnClicked(this, &SMetaHumanCalibrationImageViewer::OnNextSelectedFrameClicked)
					.IsEnabled(this, &SMetaHumanCalibrationImageViewer::IsPreviousNextButtonEnabled)
					[
						SNew(SImage)
						.Image(FAppStyle::GetBrush("Animation.Forward_End"))
					]
				]
				+ SHorizontalBox::Slot()
				.Padding(2.0f)
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "DetailsView.NameAreaButton")
					.Text(LOCTEXT("ResetCurrentState", "Reset State"))
					.ToolTipText(LOCTEXT("ResetCurrentState_Tooltip", "Resets the current state"))
					.HAlign(HAlign_Center)
					.OnClicked(this, &SMetaHumanCalibrationImageViewer::OnResetButtonClicked)
					[
						SNew(SImage)
						.Image(FMetaHumanCalibrationStyle::Get().GetBrush("MetaHumanCalibration.Generator.Reset"))
					]
				]
			]
		];

	ChildSlot
		[
			MainWidget.ToSharedRef()
		];

	SetCanTick(true);

	UMetaHumanCalibrationGeneratorSettings* Settings =
		GetMutableDefault<UMetaHumanCalibrationGeneratorSettings>();

	if (!SingleImageViewers.IsEmpty())
	{
		ScrubberSlider->SetMinAndMaxValues(0.f, SingleImageViewers[0]->GetImageNum() - 1);
		PendingValueChange = 0;
		ScrubberSlider->SetValue(PendingValueChange.GetValue());

		ChessboardPointCounter.Reset(new FMetaHumanChessboardPointCounter(FVector2D(Settings->CoverageMap), GetCameraNames(), GetFrameDimensions()));
	}

	Settings->OnCoverageMapChanged().AddSP(this, &SMetaHumanCalibrationImageViewer::InvalidateCounter);
}

void SMetaHumanCalibrationImageViewer::OnClose()
{
	FMetaHumanCalibrationViewCommands::Unregister();
}

void SMetaHumanCalibrationImageViewer::ResetView()
{
	for (TSharedPtr<SMetaHumanCalibrationSingleImageViewer>& SingleImageViewer : SingleImageViewers)
	{
		SingleImageViewer->ResetView();
	}
}

void SMetaHumanCalibrationImageViewer::ResetState()
{
	OnResetButtonClicked();
}

void SMetaHumanCalibrationImageViewer::SelectCurrentFrame()
{
	OnFrameSelectedClicked();
}

void SMetaHumanCalibrationImageViewer::NextFrame(int32 InStep)
{
	int32 CurrentFrame = FMath::Floor(ScrubberSlider->GetValue());
	CurrentFrame += InStep;

	ScrubberSlider->SetValue(CurrentFrame);
	PendingValueChange = ScrubberSlider->GetValue();
}

void SMetaHumanCalibrationImageViewer::PreviousFrame(int32 InStep)
{
	int32 CurrentFrame = FMath::Floor(ScrubberSlider->GetValue());
	CurrentFrame -= InStep;

	ScrubberSlider->SetValue(CurrentFrame);
	PendingValueChange = ScrubberSlider->GetValue();
}

void SMetaHumanCalibrationImageViewer::SetDetectedPointsForFrame(int32 InFrame, FMetaHumanCalibrationPatternDetector::FDetectedFrame InDetectedFrame)
{
	if (DetectedPointsForSelectedFrames.Contains(InFrame))
	{
		return;
	}

	ChessboardPointCounter->Update(InDetectedFrame);
	DetectedPointsForSelectedFrames.Add(InFrame, MoveTemp(InDetectedFrame));
	ScrubberSlider->SetFrameState(InFrame, EFrameState::Ok);
}

SMetaHumanCalibrationImageViewer::FPairString SMetaHumanCalibrationImageViewer::GetCameraNames() const
{
	FPairString Cameras;
	Cameras.Key = CaptureData->ImageSequences[0]->GetName();
	Cameras.Value = CaptureData->ImageSequences[1]->GetName();

	return Cameras;
}

SMetaHumanCalibrationImageViewer::FPairVector SMetaHumanCalibrationImageViewer::GetFrameDimensions() const
{
	FPairVector Dimensions;
	Dimensions.Key = SingleImageViewers[0]->GetImageSize();
	Dimensions.Value = SingleImageViewers[1]->GetImageSize();

	return Dimensions;
}

SMetaHumanCalibrationImageViewer::FPairString SMetaHumanCalibrationImageViewer::GetFramePath(int32 InFrame) const
{
	FPairString Frame;
	Frame.Key = SingleImageViewers[0]->GetImagePath(InFrame);
	Frame.Value = SingleImageViewers[1]->GetImagePath(InFrame);

	return Frame;
}

SMetaHumanCalibrationImageViewer::FPairArray SMetaHumanCalibrationImageViewer::GetFramePaths() const
{
	FPairArray Frames;
	Frames.Key = SingleImageViewers[0]->GetImagePaths();
	Frames.Value = SingleImageViewers[1]->GetImagePaths();

	return Frames;
}

void SMetaHumanCalibrationImageViewer::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	if (PendingValueChange.IsSet())
	{
		UpdateImages(PendingValueChange.GetValue());
		PendingValueChange.Reset();
	}
}

void SMetaHumanCalibrationImageViewer::InvalidateCounter()
{
	const UMetaHumanCalibrationGeneratorSettings* Settings =
		GetDefault<UMetaHumanCalibrationGeneratorSettings>();
	ChessboardPointCounter->Invalidate(FVector2D(Settings->CoverageMap));

	if (!DetectedPointsForSelectedFrames.IsEmpty())
	{
		TArray<FMetaHumanCalibrationPatternDetector::FDetectedFrame> DetectedFrames;
		DetectedPointsForSelectedFrames.GenerateValueArray(DetectedFrames);
		ChessboardPointCounter->Update(DetectedFrames);
	}
}

void SMetaHumanCalibrationImageViewer::AddOverlay(FBox2d InUVRegion, const FGeometry& InAllottedGeometry, FSlateWindowElementList& OutDrawElements, int32& OutLayerId, int32 InCameraIndex)
{
	if (!SingleImageViewers.IsValidIndex(InCameraIndex))
	{
		return;
	}

	if (EnumHasAnyFlags(ShowViewOptions[InCameraIndex], EViewOptions::Coverage))
	{
		++OutLayerId;
		ShowCoverage(InUVRegion, InAllottedGeometry, OutDrawElements, OutLayerId, InCameraIndex);
	}
	
	if (EnumHasAnyFlags(ShowViewOptions[InCameraIndex], EViewOptions::DetectedPoints))
	{
		int32 CurrentFrame = FMath::Floor(ScrubberSlider->GetValue());

		if (DetectedPointsForSelectedFrames.Contains(CurrentFrame))
		{
			++OutLayerId;
			ShowSingleFrameDetectedPoints(DetectedPointsForSelectedFrames[CurrentFrame], 
										  InUVRegion, InAllottedGeometry, OutDrawElements, OutLayerId, InCameraIndex);
		}
	}

	if (EnumHasAnyFlags(ShowViewOptions[InCameraIndex], EViewOptions::AreaOfInterest))
	{
		++OutLayerId;
		ShowAreaOfInterest(InUVRegion, InAllottedGeometry, OutDrawElements, OutLayerId, InCameraIndex);
	}
}

void SMetaHumanCalibrationImageViewer::ShowCoverage(FBox2d InUVRegion, 
													const FGeometry& InAllottedGeometry,
													FSlateWindowElementList& OutDrawElements, 
													int32& OutLayerId, 
													int32 InCameraIndex)
{
	using namespace UE::MetaHuman::Points;

	const FVector2D WidgetSize = InAllottedGeometry.GetLocalSize();
	const FVector2D ImageSize = FVector2D(SingleImageViewers[InCameraIndex]->GetImageSize());

	const FString& Name = CaptureData->ImageSequences[InCameraIndex]->GetName();

	const UMetaHumanCalibrationGeneratorSettings* Settings = GetDefault<UMetaHumanCalibrationGeneratorSettings>();

	const FVector2D CellSize = ChessboardPointCounter->GetBlockSize();

	for (int32 Y = 0; Y < Settings->CoverageMap.Y; ++Y)
	{
		for (int32 X = 0; X < Settings->CoverageMap.X; ++X)
		{
			int32 Index = Y * Settings->CoverageMap.X + X;

			TOptional<int32> NumberOfPointsForBlockOpt = ChessboardPointCounter->GetCountForBlock(Name, Index);
			if (!NumberOfPointsForBlockOpt.IsSet())
			{
				continue;
			}

			int32 NumberOfPointsForBlock = NumberOfPointsForBlockOpt.GetValue();
			if (NumberOfPointsForBlock == 0)
			{
				continue;
			}

			float Value = static_cast<float>(NumberOfPointsForBlock) / Settings->NumberOfPointsThreshold;
			Value = FMath::Clamp(Value, 0.f, 1.f);
			FLinearColor Color = FLinearColor::LerpUsingHSV(FLinearColor::Red, FLinearColor::Green, Value);
			Color.A = 0.2f; // semi-transparent for the box

			FVector2D BeginCellPoint = FVector2D(X, Y) * CellSize;

			FVector2D ActualCellSize =
				MapTextureSizeToLocalWidgetSize(BeginCellPoint, CellSize, ImageSize, InUVRegion, WidgetSize);

			FVector2D RealCellSize =
				MapRealTextureSizeToLocalWidgetSize(BeginCellPoint, CellSize, ImageSize, InUVRegion, WidgetSize);

			BeginCellPoint =
				MapTexturePointToLocalWidgetSpace(BeginCellPoint, ImageSize, InUVRegion, WidgetSize);

			FSlateDrawElement::MakeBox(
				OutDrawElements,
				OutLayerId,
				InAllottedGeometry.ToPaintGeometry(ActualCellSize, FSlateLayoutTransform(FVector2D::Clamp(BeginCellPoint, FVector2D(0.0, 0.0), WidgetSize))),
				FCoreStyle::Get().GetBrush("WhiteBrush"),
				ESlateDrawEffect::NoPixelSnapping,
				Color
			);

			static FNumberFormattingOptions NumberFormattingOptions;
			NumberFormattingOptions.MaximumFractionalDigits = 1;
			NumberFormattingOptions.MinimumFractionalDigits = 1;

			FText TextValue = 
				FText::Format(FText::FromString(TEXT("{0} %")), FText::AsNumber(Value * 100, &NumberFormattingOptions));

			float Size = 8;
			FFontOutlineSettings FontSettings(0, FLinearColor::White);
			Color.A = 1.0; // solid for font
			FSlateFontInfo FontInfo = FCoreStyle::GetDefaultFontStyle("Regular", Size, FontSettings);

			FVector2D FontLocation = BeginCellPoint + (RealCellSize / 2);

			const TSharedRef<FSlateFontMeasure> FontMeasure =
				FSlateApplication::Get().GetRenderer()->GetFontMeasureService();

			FVector2D FontSize = FontMeasure->Measure(TextValue, FontInfo);

			FontLocation.X -= FontSize.X / 2;
			FontLocation.Y -= FontSize.Y / 2;

			if (!IsOutsideWidgetBounds(FontLocation, WidgetSize) &&
				!IsOutsideWidgetBounds(FontLocation + FontSize, WidgetSize))
			{
				FSlateDrawElement::MakeText(OutDrawElements,
											OutLayerId,
											InAllottedGeometry.ToPaintGeometry(InAllottedGeometry.Size, FSlateLayoutTransform(FontLocation)),
											TextValue,
											FontInfo,
											ESlateDrawEffect::NoPixelSnapping,
											Color);
			}
		}
	}
}

void SMetaHumanCalibrationImageViewer::ShowSingleFrameDetectedPoints(const FMetaHumanCalibrationPatternDetector::FDetectedFrame& InDetectedFrame, 
																	 FBox2d InUVRegion, 
																	 const FGeometry& InAllottedGeometry, 
																	 FSlateWindowElementList& OutDrawElements, 
																	 int32& OutLayerId, 
																	 int32 InCameraIndex)
{
	using namespace UE::MetaHuman::Points;

	const FVector2D WidgetSize = InAllottedGeometry.GetLocalSize();
	const FIntVector2 ImageSize = SingleImageViewers[InCameraIndex]->GetImageSize();

	const FVector2D ImageSizeVector(ImageSize.X, ImageSize.Y);

	if (ImageSizeVector.IsZero())
	{
		return;
	}

	const TArray<FVector2D>& DetectedPoints = InDetectedFrame[CaptureData->ImageSequences[InCameraIndex]->GetName()];

	TArray<FVector2D> LinePoints;
	for (int32 PointIndex = 0; PointIndex < DetectedPoints.Num(); ++PointIndex)
	{
		FVector2D ScaledCameraPoint = MapTexturePointToLocalWidgetSpace(DetectedPoints[PointIndex], ImageSizeVector, InUVRegion, WidgetSize);

		if (IsOutsideWidgetBounds(ScaledCameraPoint, WidgetSize))
		{
			continue;
		}

		FSlateDrawElement::MakeBox(
			OutDrawElements,
			OutLayerId,
			InAllottedGeometry.ToPaintGeometry(FVector2D(3.0f), FSlateLayoutTransform(ScaledCameraPoint)),
			FCoreStyle::Get().GetBrush("WhiteBrush"),
			ESlateDrawEffect::None,
			FColor::Green
		);

		LinePoints.Add(MoveTemp(ScaledCameraPoint));
	}

	FSlateDrawElement::MakeLines(
		OutDrawElements,
		OutLayerId,
		InAllottedGeometry.ToPaintGeometry(),
		LinePoints,
		ESlateDrawEffect::None,
		FColor::Turquoise,
		true,
		1.0
	);
}

void SMetaHumanCalibrationImageViewer::ShowAreaOfInterest(FBox2d InUVRegion, const FGeometry& InAllottedGeometry,
														  FSlateWindowElementList& OutDrawElements, int32& OutLayerId, int32 InCameraIndex)
{
	using namespace UE::MetaHuman::Points;

	const FVector2D WidgetSize = InAllottedGeometry.GetLocalSize();
	const FIntVector2 ImageSize = SingleImageViewers[InCameraIndex]->GetImageSize();

	const FVector2D ImageSizeVector(ImageSize.X, ImageSize.Y);

	if (ImageSizeVector.IsZero())
	{
		return;
	}

	TSharedPtr<FMetaHumanCalibrationGeneratorState> SharedState = State.Pin();
	if (!SharedState)
	{
		return;
	}

	FBox2D AreaOfInterest = SharedState->Options->AreaOfInterestsForCameras[InCameraIndex].GetBox2D();

	// No need to show the area of interest if the area is the full image
	if (AreaOfInterest.Min == FVector2D::ZeroVector &&
		AreaOfInterest.Max == ImageSizeVector)
	{
		return;
	}

	FVector2D ActualCellSize =
		MapTextureSizeToLocalWidgetSize(AreaOfInterest.Min, AreaOfInterest.GetSize(), ImageSizeVector, InUVRegion, WidgetSize);

	FVector2D TopLeft = 
		MapTexturePointToLocalWidgetSpace(AreaOfInterest.Min, ImageSizeVector, InUVRegion, WidgetSize);

	TopLeft = FVector2D::Clamp(TopLeft, FVector2D::ZeroVector, WidgetSize);

	FSlateDrawElement::MakeBox(
		OutDrawElements,
		OutLayerId,
		InAllottedGeometry.ToPaintGeometry(ActualCellSize, FSlateLayoutTransform(TopLeft)),
		FAppStyle::GetBrush(TEXT("MarqueeSelection")),
		ESlateDrawEffect::NoPixelSnapping
	);
}

void SMetaHumanCalibrationImageViewer::UpdateImages(const int32 InFrameNumber)
{
	for (TSharedPtr<SMetaHumanCalibrationSingleImageViewer> ImageViewer : SingleImageViewers)
	{
		ImageViewer->ShowImage(InFrameNumber);
	}
}

void SMetaHumanCalibrationImageViewer::OnScrubberValueChanged(float InValue)
{
	int32 Value = FMath::Floor(InValue);
	PendingValueChange = Value;
}

FReply SMetaHumanCalibrationImageViewer::OnFrameSelectedClicked()
{
	int32 CurrentFrame = FMath::Floor(ScrubberSlider->GetValue());

	if (CurrentFrame < 0)
	{
		return FReply::Handled();
	}

	SelectFrame(CurrentFrame);

	return FReply::Handled();
}

void SMetaHumanCalibrationImageViewer::SelectFrame(int32 InFrame)
{
	if (TSharedPtr<FMetaHumanCalibrationGeneratorState> SharedState = State.Pin())
	{
		if (SharedState->Options->SelectedFrames.Contains(InFrame))
		{
			SharedState->Options->SelectedFrames.Remove(InFrame);
			DetectedPointsForSelectedFrames.Remove(InFrame);

			TArray<FMetaHumanCalibrationPatternDetector::FDetectedFrame> DetectedFrames;
			DetectedPointsForSelectedFrames.GenerateValueArray(DetectedFrames);

			ChessboardPointCounter->Invalidate(DetectedFrames);

			ScrubberSlider->RemoveFrameState(InFrame);
		}
		else
		{
			FMetaHumanCalibrationPatternDetector::FDetectedFrame DetectedPattern = RunDetectForFrame(InFrame);
			if (!DetectedPattern.IsEmpty())
			{
				ChessboardPointCounter->Update(DetectedPattern);
				DetectedPointsForSelectedFrames.Add(InFrame, MoveTemp(DetectedPattern));
				SharedState->Options->SelectedFrames.AddUnique(InFrame);

				ScrubberSlider->SetFrameState(InFrame, EFrameState::Ok);
			}
			else
			{
				ScrubberSlider->SetFrameState(InFrame, EFrameState::Bad);
			}
		}

		FrameSelected.ExecuteIfBound(InFrame);
	}
}

FMetaHumanCalibrationPatternDetector::FDetectedFrame SMetaHumanCalibrationImageViewer::RunDetectForFrame(int32 InFrame)
{
	if (!DetectForFrame.IsBound())
	{
		return FMetaHumanCalibrationPatternDetector::FDetectedFrame();
	}

	return DetectForFrame.Execute(InFrame);
}

const FSlateBrush* SMetaHumanCalibrationImageViewer::GetSelectFrameButtonImage() const
{
	if (TSharedPtr<FMetaHumanCalibrationGeneratorState> SharedState = State.Pin())
	{
		int32 CurrentFrame = FMath::Floor(ScrubberSlider->GetValue());

		if (SharedState->Options->SelectedFrames.Contains(CurrentFrame))
		{
			return FMetaHumanCalibrationStyle::Get().GetBrush("MetaHumanCalibration.Generator.Delete");
		}
	}

	return FMetaHumanCalibrationStyle::Get().GetBrush("MetaHumanCalibration.Generator.Add");
}

FReply SMetaHumanCalibrationImageViewer::OnNextSelectedFrameClicked()
{
	if (TSharedPtr<FMetaHumanCalibrationGeneratorState> SharedState = State.Pin())
	{
		int32 CurrentFrame = FMath::Floor(ScrubberSlider->GetValue());

		TArray<int32> SelectedItems = SharedState->Options->SelectedFrames;
		SelectedItems.Sort();

		int32* Found = SelectedItems.FindByPredicate([CurrentFrame](int32 InSelectedItem)
													 {
														 return CurrentFrame < InSelectedItem;
													 });
		if (Found)
		{
			PendingValueChange = *Found;
			ScrubberSlider->SetValue(*Found);
		}
	}
	
	return FReply::Handled();
}

FReply SMetaHumanCalibrationImageViewer::OnPreviousSelectedFrameClicked()
{
	if (TSharedPtr<FMetaHumanCalibrationGeneratorState> SharedState = State.Pin())
	{
		int32 CurrentFrame = FMath::Floor(ScrubberSlider->GetValue());

		TArray<int32> SelectedItems = SharedState->Options->SelectedFrames;
		SelectedItems.Sort(TGreater<>());

		int32* Found = SelectedItems.FindByPredicate([CurrentFrame](int32 InSelectedItem)
													 {
														 return CurrentFrame > InSelectedItem;
													 });
		if (Found)
		{
			PendingValueChange = *Found;
			ScrubberSlider->SetValue(*Found);
		}
	}
	return FReply::Handled();
}

bool SMetaHumanCalibrationImageViewer::IsPreviousNextButtonEnabled() const
{
	if (TSharedPtr<FMetaHumanCalibrationGeneratorState> SharedState = State.Pin())
	{
		return !SharedState->Options->SelectedFrames.IsEmpty();
	}
	
	return false;
}

FReply SMetaHumanCalibrationImageViewer::OnResetButtonClicked()
{
	if (TSharedPtr<FMetaHumanCalibrationGeneratorState> SharedState = State.Pin())
	{
		SharedState->Options->SelectedFrames.Empty();
		DetectedPointsForSelectedFrames.Empty();

		if (ChessboardPointCounter)
		{
			ChessboardPointCounter->Invalidate();
		}

		ScrubberSlider->RemoveFrameStates();
	}

	return FReply::Handled();
}

TSharedPtr<SWidget> SMetaHumanCalibrationImageViewer::GetViewToolbarWidget(int32 InCameraIndex)
{
	check(CaptureData->ImageSequences.IsValidIndex(InCameraIndex));

	FSlimHorizontalToolBarBuilder ToolbarBuilder(CameraToolkitCommands[InCameraIndex], FMultiBoxCustomization::None);
	ToolbarBuilder.SetStyle(&FAppStyle::Get(), TEXT("EditorViewportToolBar"));
	ToolbarBuilder.SetLabelVisibility(EVisibility::Visible);

	const FText ViewTextName = InCameraIndex == 0 ? LOCTEXT("ViewA", "A") : LOCTEXT("ViewB", "B");
	const FName ViewName = *ViewTextName.ToString();

	ToolbarBuilder.BeginSection(ViewName);
	{
		TSharedRef<SEditorViewportToolbarMenu> ViewDisplayOptionsDropdownMenu = SNew(SEditorViewportToolbarMenu)
			.ToolTipText(FText::Format(LOCTEXT("CalibViewDisplayOptionsMenuToolTip", "Display Options for View {0}"), { ViewTextName }))
			.Label(FText::Format(LOCTEXT("CalibViewDisplayOptionsMenu", "{0}"), { ViewTextName }))
			.OnGetMenuContent(this, &SMetaHumanCalibrationImageViewer::FillDisplayOptionsForViewMenu, InCameraIndex);

		ToolbarBuilder.AddWidget(ViewDisplayOptionsDropdownMenu);
	}
	ToolbarBuilder.EndSection();

	return ToolbarBuilder.MakeWidget();
}

void SMetaHumanCalibrationImageViewer::RegisterCommandHandlers()
{
	if (CameraToolkitCommands.IsEmpty())
	{
		return;
	}

	FMetaHumanCalibrationViewCommands& Commands = FMetaHumanCalibrationViewCommands::Get();

	for (int32 Index = 0; Index < UE::MetaHuman::Private::ExpectedNumberOfCameras; ++Index)
	{
		CameraToolkitCommands[Index]->MapAction(Commands.SelectAreaOfInterest,
												FExecuteAction::CreateSP(this, &SMetaHumanCalibrationImageViewer::StartAreaOfInterestSelection, Index));

		CameraToolkitCommands[Index]->MapAction(Commands.ResetView,
												FExecuteAction::CreateSP(this, &SMetaHumanCalibrationImageViewer::ResetView, Index));

		CameraToolkitCommands[Index]->MapAction(Commands.ToggleCoverage,
												FExecuteAction::CreateSP(this, &SMetaHumanCalibrationImageViewer::ToggleView, Index, EViewOptions::Coverage),
												nullptr,
												FGetActionCheckState::CreateSP(this, &SMetaHumanCalibrationImageViewer::IsViewToggled, Index, EViewOptions::Coverage));

		CameraToolkitCommands[Index]->MapAction(Commands.ToggleDetectedPoints,
												FExecuteAction::CreateSP(this, &SMetaHumanCalibrationImageViewer::ToggleView, Index, EViewOptions::DetectedPoints),
												nullptr,
												FGetActionCheckState::CreateSP(this, &SMetaHumanCalibrationImageViewer::IsViewToggled, Index, EViewOptions::DetectedPoints));

		CameraToolkitCommands[Index]->MapAction(Commands.ToggleAreaOfInterest,
												FExecuteAction::CreateSP(this, &SMetaHumanCalibrationImageViewer::ToggleView, Index, EViewOptions::AreaOfInterest),
												nullptr,
												FGetActionCheckState::CreateSP(this, &SMetaHumanCalibrationImageViewer::IsViewToggled, Index, EViewOptions::AreaOfInterest));

		ShowViewOptions.Add(Index, EViewOptions::All);
	}
}

void SMetaHumanCalibrationImageViewer::StartAreaOfInterestSelection(int32 InCameraIndex)
{
	check(SingleImageViewers.IsValidIndex(InCameraIndex));

	SMetaHumanCalibrationSingleImageViewer::FAreaSelectionEnded SelectionEnded = 
		SMetaHumanCalibrationSingleImageViewer::FAreaSelectionEnded::CreateSP(this, &SMetaHumanCalibrationImageViewer::UpdateAfterSelection, InCameraIndex);
	SingleImageViewers[InCameraIndex]->StartSelecting(MoveTemp(SelectionEnded));
}

void SMetaHumanCalibrationImageViewer::UpdateAfterSelection(FSlateRect InSlateRect, 
															FBox2d InUvRegion, 
															FVector2D InWidgetSize, int32 InCameraIndex)
{
	using namespace UE::MetaHuman::Points;

	if (TSharedPtr<FMetaHumanCalibrationGeneratorState> SharedState = State.Pin())
	{
		check(SharedState->Options->AreaOfInterestsForCameras.IsValidIndex(InCameraIndex));
		check(SingleImageViewers.IsValidIndex(InCameraIndex));

		FVector2D TextureSize = FVector2D(SingleImageViewers[InCameraIndex]->GetImageSize());

		FVector2D TopLeft = MapWidgetPointToTextureSpace(InSlateRect.GetTopLeft(), InWidgetSize, InUvRegion, TextureSize);
		FVector2D BottomRight = MapWidgetPointToTextureSpace(InSlateRect.GetBottomRight(), InWidgetSize, InUvRegion, TextureSize);

		TopLeft = FVector2D::Clamp(TopLeft, FVector2D::ZeroVector, TextureSize);
		BottomRight = FVector2D::Clamp(BottomRight, FVector2D::ZeroVector, TextureSize);

		SharedState->Options->AreaOfInterestsForCameras[InCameraIndex].TopLeft = MoveTemp(TopLeft);
		SharedState->Options->AreaOfInterestsForCameras[InCameraIndex].BottomRight = MoveTemp(BottomRight);
	}
}

void SMetaHumanCalibrationImageViewer::ResetView(int32 InCameraIndex)
{
	check(SingleImageViewers.IsValidIndex(InCameraIndex));

	SingleImageViewers[InCameraIndex]->ResetView();
}

void SMetaHumanCalibrationImageViewer::ToggleView(int32 InIndex, EViewOptions InView)
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

ECheckBoxState SMetaHumanCalibrationImageViewer::IsViewToggled(int32 InIndex, EViewOptions InView) const
{
	return EnumHasAnyFlags(ShowViewOptions[InIndex], InView) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

TSharedRef<SWidget> SMetaHumanCalibrationImageViewer::FillDisplayOptionsForViewMenu(int32 InCameraIndex)
{
	const bool bShouldCloseWindowAfterMenuSelection = true;
	TSharedPtr<FUICommandList> CommandList = CameraToolkitCommands[InCameraIndex];
	FMenuBuilder MenuBuilder{ bShouldCloseWindowAfterMenuSelection, CommandList };

	const FMetaHumanCalibrationViewCommands& Commands = FMetaHumanCalibrationViewCommands::Get();

	MenuBuilder.BeginSection(TEXT("ViewControlExtensionSection"), LOCTEXT("ViewControlExtensionSectionLabel", "Control"));
	{
		MenuBuilder.AddMenuEntry(Commands.SelectAreaOfInterest);
		MenuBuilder.AddMenuEntry(Commands.ResetView);
	}
	MenuBuilder.EndSection();

	MenuBuilder.BeginSection(TEXT("DiagnosticsExtensionSection"), LOCTEXT("DiagnosticsExtensionSectionLabel", "Diagnostics"));
	{
		
		MenuBuilder.AddMenuEntry(Commands.ToggleCoverage);
		MenuBuilder.AddMenuEntry(Commands.ToggleDetectedPoints);
		MenuBuilder.AddMenuEntry(Commands.ToggleAreaOfInterest);
	}
	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}

#undef LOCTEXT_NAMESPACE

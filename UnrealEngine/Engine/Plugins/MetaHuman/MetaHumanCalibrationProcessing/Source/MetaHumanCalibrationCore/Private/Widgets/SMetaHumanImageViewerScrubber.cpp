// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SMetaHumanImageViewerScrubber.h"
#include "Widgets/SOverlay.h"

#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SSpacer.h"

#include "Utils/MetaHumanCalibrationUtils.h"
#include "ParseTakeUtils.h"

#define LOCTEXT_NAMESPACE "MetaHumanImageViewerScrubber"

namespace Private
{

static FLinearColor GetColorForFrameState(EFrameState::Type InState)
{
	switch (InState)
	{
		case EFrameState::Ok:
			return FLinearColor(0.07059, 0.32941, 0.07059);
		case EFrameState::Bad:
			return FLinearColor(0.32941, 0.07059, 0.07059);
		case EFrameState::Neutral:
		default:
			return FLinearColor::White;
	}
}

}

class SMetaHumanImageViewerStateVisualizer : public SLeafWidget
{
public:

	SLATE_BEGIN_ARGS(SMetaHumanImageViewerStateVisualizer)
		: _ProgressBarHeight(10.0f)
		, _FrameStateProvider(nullptr)
		{
		}

		SLATE_ARGUMENT(float, ProgressBarHeight)
		SLATE_ARGUMENT(IFrameStateProvider*, FrameStateProvider)

	SLATE_END_ARGS()

	void Construct(const FArguments& InArguments)
	{
		ProgressBarHeight = InArguments._ProgressBarHeight;
		FrameStateProvider = InArguments._FrameStateProvider;
	}

	virtual FVector2D ComputeDesiredSize(float) const override
	{
		return FVector2D(0.0, ProgressBarHeight);
	}

private:

	int32 OnPaint(const FPaintArgs& InArgs,
				  const FGeometry& InAllottedGeometry,
				  const FSlateRect& InCullingRect,
				  FSlateWindowElementList& OutDrawElements,
				  int32 InLayerId,
				  const FWidgetStyle& InWidgetStyle,
				  bool bInParentEnabled) const
	{
		if (!FrameStateProvider)
		{
			return InLayerId;
		}

		const TMap<int32, EFrameState::Type>& FrameStates = FrameStateProvider->GetFrameStates();
		int32 NumberOfFrames = FrameStateProvider->GetNumberOfFrames();

		if (FrameStates.IsEmpty() || NumberOfFrames == 0)
		{
			return InLayerId;
		}

		++InLayerId;

		float BlockSize = InAllottedGeometry.Size.X / NumberOfFrames;

		for (const TPair<int32, EFrameState::Type>& FrameStatePair : FrameStates)
		{
			FLinearColor Color = Private::GetColorForFrameState(FrameStatePair.Value);

			float ActualDrawSize = BlockSize;
			if (FrameStatePair.Key == 0 || FrameStatePair.Key == NumberOfFrames)
			{
				ActualDrawSize *= 0.5;
			}

			float DrawLocationX = BlockSize * FrameStatePair.Key;
			if (FrameStatePair.Key != 0)
			{
				DrawLocationX -= BlockSize / 2;
			}

			DrawLocationX = FMath::Clamp(DrawLocationX, 0.0f, InAllottedGeometry.Size.X);

			FSlateDrawElement::MakeBox(
				OutDrawElements,
				InLayerId,
				InAllottedGeometry.ToPaintGeometry(FVector2f(ActualDrawSize, ProgressBarHeight), FSlateLayoutTransform(FVector2f(DrawLocationX, 0.0))),
				FCoreStyle::Get().GetBrush("WhiteBrush"),
				ESlateDrawEffect::NoPixelSnapping,
				Color
			);
		}

		return InLayerId;
	}

	float ProgressBarHeight;
	IFrameStateProvider* FrameStateProvider;
};

void SMetaHumanImageViewerScrubber::Construct(const FArguments& InArgs)
{
	FrameRate = FMath::IsNearlyZero(InArgs._FrameRate) ? 60.0 : InArgs._FrameRate;

	TSharedRef<SOverlay> OverlayWidget =
		SNew(SOverlay)
		+ SOverlay::Slot()
		.VAlign(VAlign_Center)
		[
			SAssignNew(Scrubber, SSlider)
			.ToolTipText(InArgs._ToolTipText)
			.Style(InArgs._Style)
			.OnValueChanged(InArgs._OnValueChanged)
			.SliderBarColor(FLinearColor::Black)
			.IsEnabled(true)
			.IsFocusable(false)
			.MouseUsesStep(true)
			.StepSize(1.f)
			.Value(0.f)
			.MinValue(0.f)
			.MaxValue(0.f)
			.Orientation(Orient_Horizontal)
			.PreventThrottling(true)
		];

	if (InArgs._AllowVisualization)
	{
		OverlayWidget->AddSlot()
		.Padding(2.0f, 0.0f, 2.0f, 0.0f)
		.VAlign(VAlign_Center)
		[
			SNew(SMetaHumanImageViewerStateVisualizer)
			.ProgressBarHeight(InArgs._Style->BarThickness)
			.FrameStateProvider(this)
		];
	}

	ChildSlot
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SOverlay)
				+ SOverlay::Slot()
				.VAlign(VAlign_Bottom)
				[
					OverlayWidget
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 2.0f, 0.0f, 0.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.Padding(8.0f, 0.0f)
				.VAlign(VAlign_Center)
				[
					SNew(SSpacer)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(4.0f, 0.0f, 0.0f, 0.0f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(this, &SMetaHumanImageViewerScrubber::HandleTimerTextBlock)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(12.0f, 0.0f, 0.0f, 0.0f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(this, &SMetaHumanImageViewerScrubber::HandleFrameTextBlock)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(12.0f, 0.0f, 0.0f, 0.0f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(this, &SMetaHumanImageViewerScrubber::HandleFrameRateBlock)
				]
			]
		];
}

void SMetaHumanImageViewerScrubber::SetFrameStates(const TMap<int32, EFrameState::Type>& InFrameState)
{
	FrameStates = InFrameState;
}

void SMetaHumanImageViewerScrubber::SetFrameState(int32 InFrameNumber, EFrameState::Type InFrameState)
{
	FrameStates.Add(InFrameNumber, InFrameState);
}

void SMetaHumanImageViewerScrubber::RemoveFrameState(int32 InFrameNumber)
{
	FrameStates.Remove(InFrameNumber);
}

void SMetaHumanImageViewerScrubber::RemoveFrameStates()
{
	FrameStates.Empty();
}

float SMetaHumanImageViewerScrubber::GetValue() const
{
	return Scrubber->GetValue();
}

void SMetaHumanImageViewerScrubber::SetValue(float InValue)
{
	float ClampedValue = FMath::Clamp(InValue, Scrubber->GetMinValue(), Scrubber->GetMaxValue());
	Scrubber->SetValue(ClampedValue);
}

void SMetaHumanImageViewerScrubber::SetMinAndMaxValues(float InMinValue, float InMaxValue)
{
	Scrubber->SetMinAndMaxValues(InMinValue, InMaxValue);
}

int32 SMetaHumanImageViewerScrubber::GetNumberOfFrames() const
{
	return Scrubber->GetMaxValue() - Scrubber->GetMinValue();
}

const TMap<int32, EFrameState::Type>& SMetaHumanImageViewerScrubber::GetFrameStates() const
{
	return FrameStates;
}

FReply SMetaHumanImageViewerScrubber::OnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	return Scrubber->OnMouseButtonDown(InGeometry, InMouseEvent);
}

FReply SMetaHumanImageViewerScrubber::OnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	return Scrubber->OnMouseButtonUp(InGeometry, InMouseEvent);
}

void SMetaHumanImageViewerScrubber::OnMouseCaptureLost(const FCaptureLostEvent& InCaptureLostEvent)
{
	Scrubber->OnMouseCaptureLost(InCaptureLostEvent);
}

FReply SMetaHumanImageViewerScrubber::OnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	return Scrubber->OnMouseMove(InGeometry, InMouseEvent);
}

FText SMetaHumanImageViewerScrubber::HandleTimerTextBlock() const
{
	using namespace UE::MetaHuman::Image;

	FFrameNumber FrameNumber(FMath::FloorToInt(GetValue()));
	FFrameNumber LastFrameNumber(FMath::FloorToInt(Scrubber->GetMaxValue()));

	FFrameRate ConvertedFrameRate = ConvertFrameRate(FrameRate);

	const double NumberOfSeconds = FrameNumber.Value * ConvertedFrameRate.AsInterval();
	FTimespan CurrentTime = FTimespan::FromSeconds(NumberOfSeconds);

	const double DurationInSeconds = LastFrameNumber.Value * ConvertedFrameRate.AsInterval();
	FTimespan Duration = FTimespan::FromSeconds(DurationInSeconds);

	FString CurrentTimeString = GetStringFromTimespan(CurrentTime);
	FString DurationString = GetStringFromTimespan(Duration);

	return FText::Format(LOCTEXT("TimerTextBlock", "{0} / {1}"), FText::FromString(CurrentTimeString), FText::FromString(DurationString));
}

FText SMetaHumanImageViewerScrubber::HandleFrameTextBlock() const
{
	FFrameNumber FrameNumber(FMath::FloorToInt(GetValue()));
	FFrameNumber LastFrameNumber(FMath::FloorToInt(Scrubber->GetMaxValue()));

	return FText::Format(LOCTEXT("FrameTextBlock", "{0} / {1}"), FText::AsNumber(FrameNumber.Value), FText::AsNumber(LastFrameNumber.Value));
}

FText SMetaHumanImageViewerScrubber::HandleFrameRateBlock() const
{
	return ConvertFrameRate(FrameRate).ToPrettyText();
}

#undef LOCTEXT_NAMESPACE
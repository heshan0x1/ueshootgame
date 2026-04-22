// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/Input/SSlider.h"
#include "Widgets/SCompoundWidget.h"

#include "Containers/Map.h"

#define UE_API METAHUMANCALIBRATIONCORE_API

namespace EFrameState
{

enum Type
{
	Ok = 0,
	Bad = 1,
	Neutral = 2
};

}

class IFrameStateProvider
{
public:

	virtual ~IFrameStateProvider() = default;
	virtual const TMap<int32, EFrameState::Type>& GetFrameStates() const = 0;
	virtual int32 GetNumberOfFrames() const = 0;
};

class SMetaHumanImageViewerScrubber : public SCompoundWidget, public IFrameStateProvider
{
public:

	SLATE_BEGIN_ARGS(SMetaHumanImageViewerScrubber)
		: _OnValueChanged()
		, _Style()
		, _NumberOfFrames(0)
		, _FrameRate(0.0)
		, _AllowVisualization(true)
		{
		}

		SLATE_EVENT(FOnFloatValueChanged, OnValueChanged)
		SLATE_STYLE_ARGUMENT(FSliderStyle, Style)

		SLATE_ARGUMENT(int32, NumberOfFrames)
		SLATE_ARGUMENT(double, FrameRate)
		SLATE_ARGUMENT(bool, AllowVisualization)
	SLATE_END_ARGS()

	UE_API void Construct(const FArguments& InArgs);

	UE_API void SetFrameStates(const TMap<int32, EFrameState::Type>& InFrameStates);
	UE_API void SetFrameState(int32 InFrameNumber, EFrameState::Type InFrameState);
	UE_API void RemoveFrameState(int32 InFrameNumber);
	UE_API void RemoveFrameStates();

	// SSlider API
	UE_API float GetValue() const;
	UE_API void SetValue(float InValue);
	UE_API void SetMinAndMaxValues(float InMinValue, float InMaxValue);

	UE_API virtual int32 GetNumberOfFrames() const override;
	UE_API virtual const TMap<int32, EFrameState::Type>& GetFrameStates() const override;

private:

	UE_API virtual FReply OnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	UE_API virtual FReply OnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	UE_API virtual void OnMouseCaptureLost(const FCaptureLostEvent& InCaptureLostEvent) override;
	UE_API virtual FReply OnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	// End SSlider API

	FText HandleTimerTextBlock() const;
	FText HandleFrameTextBlock() const;
	FText HandleFrameRateBlock() const;

	TSharedPtr<SSlider> Scrubber;

	TMap<int32, EFrameState::Type> FrameStates;
	double FrameRate;
};

#undef UE_API
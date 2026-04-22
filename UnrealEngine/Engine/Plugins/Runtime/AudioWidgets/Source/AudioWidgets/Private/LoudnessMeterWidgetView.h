// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/ObservableArray.h"
#include "Framework/SlateDelegates.h"
#include "Widgets/SWidget.h"

namespace AudioWidgets
{
	/**
	 * A widget that displays various loudness metrics as numeric values and/or meters.
	 * Could be moved to AudioWidgetsCore Public once API is stable.
	 */
	class FLoudnessMeterWidgetView
	{
	public:
		struct FLoudnessMetric
		{
			FName Name;
			FText DisplayName;
			TAttribute<TOptional<float>> Value;
			TAttribute<bool> bShowValue = false;
			TAttribute<bool> bShowMeter = false;
			FSimpleDelegate OnShowValueToggleRequested;
			FSimpleDelegate OnShowMeterToggleRequested;
		};

		struct FTimerPanelParams
		{
			TAttribute<FTimespan> AnalysisTime = FTimespan::Zero();
			FOnClicked OnResetButtonClicked;
			TAttribute<bool> bIsVisible = false;
			FSimpleDelegate OnVisibilityToggleRequested;
		};

		FLoudnessMeterWidgetView();
		~FLoudnessMeterWidgetView();

		void InitTimerPanel(const FTimerPanelParams& InTimerPanelParams);

		TSharedRef<SWidget> MakeWidget() const;

		void AddLoudnessMetric(const FLoudnessMetric& LoudnessMetric);
		void RefreshVisibleLoudnessMetrics();

	private:
		using FLoudnessMetricRef = TSharedRef<const FLoudnessMetric>;
		using FLoudnessMetricRefArray = UE::Slate::Containers::TObservableArray<FLoudnessMetricRef>;

		TSharedRef<FLoudnessMetricRefArray> LoudnessMetrics;
		TSharedRef<FLoudnessMetricRefArray> NumericValues;
		TSharedRef<FLoudnessMetricRefArray> MeterValues;
		FTimerPanelParams TimerPanelParams;
	};
} // namespace AudioWidgets

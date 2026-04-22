// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "AudioAnalyzerRack.h"
#include "Containers/Ticker.h"
#include "LKFS.h"
#include "LoudnessMeterWidgetView.h"

namespace AudioWidgets
{
	/**
	 * Constructor parameters for the analyzer.
	 */
	struct FAudioLoudnessMeterParams
	{
		int32 NumChannels = 1;
		Audio::FDeviceId AudioDeviceId = INDEX_NONE;
		TObjectPtr<UAudioBus> ExternalAudioBus = nullptr;
	};

	/**
	 * Owns an analyzer and a corresponding Slate widget for displaying the loudness stats.
	 * Can either create an Audio Bus to analyze, or analyze the given Bus.
	 */
	class FAudioLoudnessMeter : public IAudioAnalyzerRackUnit
	{
	public:
		static const FAudioAnalyzerRackUnitTypeInfo RackUnitTypeInfo;
		
		FAudioLoudnessMeter(const FAudioLoudnessMeterParams& Params);
		~FAudioLoudnessMeter();

		// Begin IAudioAnalyzerRackUnit overrides.
		virtual void SetAudioBusInfo(const FAudioBusInfo& AudioBusInfo) override;
		virtual TSharedRef<SDockTab> SpawnTab(const FSpawnTabArgs& Args) const override;
		// End IAudioAnalyzerRackUnit overrides.

	private:
		static TSharedRef<IAudioAnalyzerRackUnit> MakeRackUnit(const FAudioAnalyzerRackUnitConstructParams& Params);

		void Init(int32 InNumChannels, Audio::FDeviceId InAudioDeviceId, TObjectPtr<UAudioBus> InExternalAudioBus);
		void Teardown();

		void CreateLKFSAnalyzer();
		void ReleaseLKFSAnalyzer();

		void InitLoudnessMeterWidgetView(const FAudioAnalyzerRackUnitConstructParams& Params);

		void OnLoudnessOutput(ULKFSAnalyzer* InLKFSAnalyzer, const FLKFSResults& InLoudnessResults);

		FTimespan GetAnalysisTime() const;
		FReply HandleResetButtonClicked();

		/** Creates the meters widget */
		FLoudnessMeterWidgetView LoudnessMeterWidgetView;
		
		/** Audio analyzer object. */
		TStrongObjectPtr<ULKFSAnalyzer> LKFSAnalyzer;

		/** Analyzer settings. */
		TStrongObjectPtr<ULKFSSettings> LKFSSettings;

		/** Handle for results delegate for analyzer. */
		FDelegateHandle LatestOverallLKFSResultsDelegateHandle;

		/** Most recent results from the analyzer. */
		TOptional<FLKFSResults> LatestLoudnessResults;

		Audio::FDeviceId AudioDeviceId = INDEX_NONE;

		FTSTicker::FDelegateHandle WidgetRefreshTicker;

		/** The audio bus used for analysis. */
		TStrongObjectPtr<UAudioBus> AudioBus;
		bool bUseExternalAudioBus = false;
	};
} // namespace AudioWidgets

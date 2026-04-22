// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Delegates/Delegate.h"
#include "Insights/ITimingViewExtender.h"
#include "Math/Range.h"
#include "Misc/Optional.h"

namespace Insights
{
	class ITimingViewSession;
	enum class ETimeChangedFlags : int32;
}

class IAnalysisSession;

namespace UE::Audio::Insights
{
	// How the cache manage handles new incoming messages
	// Cache - will save the incoming message inside the cache to be retrievable later
	// Process - sends the message to the Audio Insights providers to be processed and displayed in the UI
	enum class ECacheAndProcess
	{
		Latest = 0,				// Cache and process all new messages
		CacheLatestNoProcess,	// Cache new messages but do not process them
		None					// Do not cache or process any new messages
	};

	enum class ESystemControllingTimeMarker
	{
		EventLog = 0,
		PlotsWidget,
		External
	};

	class  FAudioInsightsTimingViewExtender : public UE::Insights::Timing::ITimingViewExtender
	{
	public:
		// Insights::ITimingViewExtender interface
		virtual void OnBeginSession(UE::Insights::Timing::ITimingViewSession& InSession) override;
		virtual void OnEndSession(UE::Insights::Timing::ITimingViewSession& InSession) override;
		virtual void Tick(UE::Insights::Timing::ITimingViewSession& InSession, const TraceServices::IAnalysisSession& InAnalysisSession) override;

		ECacheAndProcess GetMessageCacheAndProcessingStatus() const { return CacheAndProcessMessageStatus; }
		TOptional<ESystemControllingTimeMarker> TryGetSystemControllingTimeMarker() const { return SystemControllingTime; }

		void StopProcessingNewMessages() { CacheAndProcessMessageStatus = ECacheAndProcess::CacheLatestNoProcess; }
		void StopCachingAndProcessingNewMessages() { CacheAndProcessMessageStatus = ECacheAndProcess::None; }
		void ResetMessageProcessType();

		double GetCurrentDurationSeconds() const;
		TRange<double> GetPlottingRange() const { return PlottingRange; }

		void SetTimeMarker(const double Timestamp, const ESystemControllingTimeMarker ControllingSystem, TOptional<TRange<double>> InPlottingRange = TOptional<TRange<double>>());

		DECLARE_MULTICAST_DELEGATE_OneParam(FOnTimingViewTimeMarkerChanged, double /*TimeMarker*/);
		FOnTimingViewTimeMarkerChanged OnTimingViewTimeMarkerChanged;

		DECLARE_MULTICAST_DELEGATE(FOnTimeControlMethodReset);
		FOnTimeControlMethodReset OnTimeControlMethodReset;

		// Move to AudioInsightsConstants
		static constexpr double MaxPlottingHistorySeconds = 5.0;
		static constexpr double PlottingMarginSeconds = 0.2;

	private:
		void OnTimeMarkerChanged(UE::Insights::Timing::ETimeChangedFlags InFlags, double InTimeMarker);

		const TraceServices::IAnalysisSession* AnalysisSession = nullptr;
		UE::Insights::Timing::ITimingViewSession* TimingView = nullptr;

		ECacheAndProcess CacheAndProcessMessageStatus = ECacheAndProcess::Latest;
		TOptional<ESystemControllingTimeMarker> SystemControllingTime;
		TRange<double> PlottingRange { 0.0, MaxPlottingHistorySeconds };
		double TraceDurationSeconds = 0.0;
		bool bUserInputDetected = false;
	};
} // namespace UE::Audio::Insights

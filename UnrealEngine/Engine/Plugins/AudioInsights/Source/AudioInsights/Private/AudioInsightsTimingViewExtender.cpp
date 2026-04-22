// Copyright Epic Games, Inc. All Rights Reserved.
#include "AudioInsightsTimingViewExtender.h"

#include "Insights/ITimingViewSession.h"
#include "TraceServices/Model/AnalysisSession.h"

namespace UE::Audio::Insights
{
	void FAudioInsightsTimingViewExtender::OnBeginSession(UE::Insights::Timing::ITimingViewSession& InSession)
	{
		if (TimingView == nullptr)
		{
			TimingView = &InSession;
		}

		SystemControllingTime.Reset();

		InSession.OnTimeMarkerChanged().AddRaw(this, &FAudioInsightsTimingViewExtender::OnTimeMarkerChanged);
	}

	void FAudioInsightsTimingViewExtender::OnEndSession(UE::Insights::Timing::ITimingViewSession& InSession)
	{
		InSession.OnTimeMarkerChanged().RemoveAll(this);

		TimingView = nullptr;
	}

	void FAudioInsightsTimingViewExtender::Tick(UE::Insights::Timing::ITimingViewSession& InSession, const TraceServices::IAnalysisSession& InAnalysisSession)
	{
		AnalysisSession = &InAnalysisSession;

		{
			TraceServices::FAnalysisSessionReadScope SessionReadScope(InAnalysisSession);
			TraceDurationSeconds = InAnalysisSession.GetDurationSeconds();
		}
	}

	void FAudioInsightsTimingViewExtender::ResetMessageProcessType()
	{
		CacheAndProcessMessageStatus = ECacheAndProcess::Latest;
		SystemControllingTime.Reset();

		OnTimeControlMethodReset.Broadcast();
	}

	double FAudioInsightsTimingViewExtender::GetCurrentDurationSeconds() const
	{
		return TraceDurationSeconds;
	}

	void FAudioInsightsTimingViewExtender::SetTimeMarker(const double Timestamp, const ESystemControllingTimeMarker ControllingSystem, TOptional<TRange<double>> InPlottingRange /*= TOptional<TRange<double>>()*/)
	{
		SystemControllingTime = ControllingSystem;
		bUserInputDetected = true;

		if (InPlottingRange.IsSet())
		{
			PlottingRange = InPlottingRange.GetValue();
		}
		else
		{
			PlottingRange = TRange<double>(Timestamp - MaxPlottingHistorySeconds, Timestamp);
		}

		// If you set the time marker on the TimingView, it will Broadcast OnTimingViewTimeMarkerChanged. Otherwise we gotta do it ourselves.
		if (TimingView != nullptr)
		{
			TimingView->SetAndCenterOnTimeMarker(Timestamp);
		}
		else
		{
			OnTimingViewTimeMarkerChanged.Broadcast(Timestamp);
		}
	}

	void FAudioInsightsTimingViewExtender::OnTimeMarkerChanged(UE::Insights::Timing::ETimeChangedFlags InFlags, double InTimeMarker)
	{
		if (AnalysisSession == nullptr)
		{
			bUserInputDetected = false;
			return;
		}
		
		TraceServices::FAnalysisSessionReadScope SessionReadScope(*AnalysisSession);

		const bool bIsValidTime = InTimeMarker >= 0.0 && InTimeMarker <= AnalysisSession->GetDurationSeconds();

		// Scrub Audio Insights when all data is loaded in non-live sessions
		if (AnalysisSession->IsAnalysisComplete() && bIsValidTime)
		{
			if (!bUserInputDetected)
			{
				SystemControllingTime = ESystemControllingTimeMarker::External;
				PlottingRange = TRange<double>(InTimeMarker - MaxPlottingHistorySeconds, InTimeMarker);
			}

			OnTimingViewTimeMarkerChanged.Broadcast(InTimeMarker);
		}
		else
		{
			SystemControllingTime.Reset();
		}

		bUserInputDetected = false;
	}
} // namespace UE::Audio::Insights

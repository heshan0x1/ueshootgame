// Copyright Epic Games, Inc. All Rights Reserved.

#include "RegionsTimingTrack.h"

#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "HAL/PlatformApplicationMisc.h"

// TraceServices
#include "Common/ProviderLock.h"
#include "TraceServices/Model/Regions.h"

// TraceInsightsCore
#include "InsightsCore/Common/Log.h"
#include "InsightsCore/Common/TimeUtils.h"
#include "InsightsCore/Filter/ViewModels/FilterConfigurator.h"
#include "InsightsCore/Filter/ViewModels/Filters.h"

// TraceInsights
#include "Insights/Common/InsightsMenuBuilder.h"
#include "Insights/InsightsManager.h"
#include "Insights/InsightsStyle.h"
#include "Insights/ITimingViewSession.h"
#include "Insights/TimingProfilerCommon.h"
#include "Insights/TimingProfiler/ViewModels/TimingRegionsSharedState.h"
#include "Insights/ViewModels/TimingTrackViewport.h"
#include "Insights/ViewModels/TimingViewDrawHelper.h"
#include "Insights/Widgets/STimingView.h"

#define LOCTEXT_NAMESPACE "UE::Insights::TimingProfiler::TimingRegions"

namespace UE::Insights::TimingProfiler
{


////////////////////////////////////////////////////////////////////////////////////////////////////
// FTimingRegionsTrack
////////////////////////////////////////////////////////////////////////////////////////////////////

INSIGHTS_IMPLEMENT_RTTI(FTimingRegionsTrack)

FTimingRegionsTrack::~FTimingRegionsTrack()
{
}


void FTimingRegionsTrack::SetRegionsCategory(const TCHAR* InRegionsCategory)
{
	RegionsCategory = InRegionsCategory;
	SetName(FString(TEXT("Timing Regions - ")) + InRegionsCategory);
}

void FTimingRegionsTrack::BuildContextMenu(FMenuBuilder& MenuBuilder)
{
	FTimingEventsTrack::BuildContextMenu(MenuBuilder);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimingRegionsTrack::InitTooltip(FTooltipDrawState& InOutTooltip, const ITimingEvent& InTooltipEvent) const
{
	if (InTooltipEvent.CheckTrack(this) && InTooltipEvent.Is<FTimingEvent>())
	{
		const FTimingEvent& TooltipEvent = InTooltipEvent.As<FTimingEvent>();

		auto MatchEvent = [this, &TooltipEvent](double InStartTime, double InEndTime, uint32 InDepth)
		{
			return InDepth == TooltipEvent.GetDepth()
				&& InStartTime == TooltipEvent.GetStartTime()
				&& InEndTime == TooltipEvent.GetEndTime();
		};

		FTimingEventSearchParameters SearchParameters(TooltipEvent.GetStartTime(), TooltipEvent.GetEndTime(), ETimingEventSearchFlags::StopAtFirstMatch, MatchEvent);
		FindRegionEvent(SearchParameters,
			[this, &InOutTooltip, &TooltipEvent]
			(double InFoundStartTime, double InFoundEndTime, uint32 InFoundDepth, const TraceServices::FTimeRegion& InRegion)
			{
				check(InRegion.Timer);
				InOutTooltip.Reset();
				InOutTooltip.AddTitle(InRegion.Timer->Name, FLinearColor::White);
				InOutTooltip.AddNameValueTextLine(TEXT("Duration:"), FormatTimeAuto(InRegion.EndTime - InRegion.BeginTime));
				InOutTooltip.AddNameValueTextLine(TEXT("Depth:"), FString::FromInt(InRegion.Depth));
				if (InRegion.Timer->Category && InRegion.Timer->Category->Name)
				{
					InOutTooltip.AddNameValueTextLine(TEXT("Category:"), InRegion.Timer->Category->Name);
				}
				InOutTooltip.UpdateLayout();
			});
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimingRegionsTrack::BuildDrawState(
	ITimingEventsTrackDrawStateBuilder& Builder,
	const ITimingTrackUpdateContext& Context)
{
	const FTimingTrackViewport& Viewport = Context.GetViewport();
	TSharedPtr<const TraceServices::IAnalysisSession> Session = FInsightsManager::Get()->GetSession();

	const TraceServices::IRegionProvider& RegionProvider = TraceServices::ReadRegionProvider(*Session);
	TraceServices::FProviderReadScopeLock RegionProviderScopedLock(RegionProvider);

	FStopwatch Stopwatch;
	Stopwatch.Start();

	// We are counting only non-empty lanes, so we can collapse empty ones in the visualization.
	int32 CurDepth = 0;
	const TraceServices::IRegionTimeline* Timeline = RegionProvider.GetTimelineForCategory(RegionsCategory);
	check(Timeline)

	Timeline->EnumerateLanes(
		[this, Viewport, &CurDepth, &Builder]
		(const TraceServices::FRegionLane& Lane, const int32 Depth)
		{
			bool RegionHadEvents = false;
			Lane.EnumerateRegions(Viewport.GetStartTime(), Viewport.GetEndTime(),
				[this, &Builder, &RegionHadEvents, &CurDepth]
				(const TraceServices::FTimeRegion& Region) -> bool
				{
					check(Region.Timer);
					RegionHadEvents = true;
					uint32 EventColor = SharedState.bColorRegionsByCategory ?
						FTimingEvent::ComputeEventColor(Region.Timer->Category ? Region.Timer->Category->Name : nullptr) :
						FTimingEvent::ComputeEventColor(Region.Timer->Name);
					Builder.AddEvent(Region.BeginTime, Region.EndTime,CurDepth, Region.Timer->Name, 0, EventColor);
					return true;
				});

			if (RegionHadEvents)
			{
				CurDepth++;
			}
		});

	Stopwatch.Stop();
	const double TotalTime = Stopwatch.GetAccumulatedTime();
	UE_CLOG(TotalTime > 1.0, LogTimingProfiler, Verbose, TEXT("[Regions] Updated draw state in %s."), *FormatTimeAuto(TotalTime));
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimingRegionsTrack::BuildFilteredDrawState(
	ITimingEventsTrackDrawStateBuilder& Builder,
	const ITimingTrackUpdateContext& Context)
{
	const TSharedPtr<ITimingEventFilter> EventFilterPtr = Context.GetEventFilter();
	if (EventFilterPtr.IsValid() && EventFilterPtr->FilterTrack(*this))
	{
		bool bFilterOnlyByEventType = false; // this is the most often use case, so the below code tries to optimize it
		TCHAR* FilterEventType = 0;
		if (EventFilterPtr->Is<FTimingEventFilterByEventType>())
		{
			bFilterOnlyByEventType = true;
			const FTimingEventFilterByEventType& EventFilter = EventFilterPtr->As<FTimingEventFilterByEventType>();
			FilterEventType = reinterpret_cast<TCHAR*>(EventFilter.GetEventType());
		}

		TSharedPtr<const TraceServices::IAnalysisSession> Session = FInsightsManager::Get()->GetSession();
		if (Session.IsValid())
		{
			const TraceServices::IRegionProvider& RegionProvider = TraceServices::ReadRegionProvider(*Session);
			TraceServices::FProviderReadScopeLock RegionProviderScopedLock(RegionProvider);

			const FTimingTrackViewport& Viewport = Context.GetViewport();

			if (bFilterOnlyByEventType)
			{
				int32 CurDepth = 0;
				const TraceServices::IRegionTimeline* Timeline = RegionProvider.GetTimelineForCategory(RegionsCategory);
				check(Timeline)
				Timeline->EnumerateLanes(
					[this, Viewport, &CurDepth, &Builder, FilterEventType]
					(const TraceServices::FRegionLane& Lane, const int32 Depth)
					{
						bool RegionHadEvents = false;
						Lane.EnumerateRegions(Viewport.GetStartTime(), Viewport.GetEndTime(),
							[&Builder, &RegionHadEvents, &CurDepth, FilterEventType]
							(const TraceServices::FTimeRegion& Region) -> bool
							{
								check(Region.Timer);
								RegionHadEvents = true;
								if (Region.Timer->Name == FilterEventType)
								{
									Builder.AddEvent(Region.BeginTime, Region.EndTime, CurDepth, Region.Timer->Name);
								}
								return true;
							});

						if (RegionHadEvents)
						{
							CurDepth++;
						}
					});
			}
			else // generic filter
			{
				//TODO: if (EventFilterPtr->FilterEvent(TimingEvent))
			}
		}
	}

	if (HasCustomFilter())
	{
		if (!FilterConfigurator.IsValid())
		{
			return;
		}

		FFilterContext FilterContext;
		FilterContext.SetReturnValueForUnsetFilters(false);

		FilterContext.AddFilterData<double>(static_cast<int32>(EFilterField::StartTime), 0.0f);
		FilterContext.AddFilterData<double>(static_cast<int32>(EFilterField::EndTime), 0.0f);
		FilterContext.AddFilterData<double>(static_cast<int32>(EFilterField::Duration), 0.0f);
		FilterContext.AddFilterData<FString>(static_cast<int32>(EFilterField::TrackName), this->GetName());
		FilterContext.AddFilterData<FString>(static_cast<int32>(EFilterField::RegionName), "");
		FilterContext.AddFilterData<FString>(static_cast<int32>(EFilterField::RegionCategory), "");

		TSharedPtr<const TraceServices::IAnalysisSession> Session = FInsightsManager::Get()->GetSession();

		if (Session.IsValid())
		{
			const TraceServices::IRegionProvider& RegionProvider = TraceServices::ReadRegionProvider(*Session);
			TraceServices::FProviderReadScopeLock RegionProviderScopedLock(RegionProvider);
			const FTimingTrackViewport& Viewport = Context.GetViewport();

			int32 CurDepth = 0;
			const TraceServices::IRegionTimeline* Timeline = RegionProvider.GetTimelineForCategory(RegionsCategory);
			check(Timeline)
			Timeline->EnumerateLanes(
				[this, Viewport, &CurDepth, &Builder, &FilterContext]
				(const TraceServices::FRegionLane& Lane, const int32 Depth)
				{
					bool RegionHadEvents = false;
					Lane.EnumerateRegions(Viewport.GetStartTime(), Viewport.GetEndTime(),
						[&Builder, &RegionHadEvents, &CurDepth, &FilterContext, this]
						(const TraceServices::FTimeRegion& Region) -> bool
						{
							check(Region.Timer);
							FilterContext.SetFilterData<double>(static_cast<int32>(EFilterField::StartTime), Region.BeginTime);
							FilterContext.SetFilterData<double>(static_cast<int32>(EFilterField::EndTime), Region.EndTime);
							FilterContext.SetFilterData<double>(static_cast<int32>(EFilterField::Duration), Region.EndTime - Region.BeginTime);
							FilterContext.SetFilterData<FString>(static_cast<int32>(EFilterField::RegionName), Region.Timer->Name);
							FilterContext.SetFilterData<FString>(static_cast<int32>(EFilterField::RegionCategory), Region.Timer->Category ? Region.Timer->Category->Name : nullptr);

							RegionHadEvents = true;
							if (FilterConfigurator->ApplyFilters(FilterContext))
							{
								Builder.AddEvent(Region.BeginTime, Region.EndTime, CurDepth, Region.Timer->Name);
							}

							return true;
						});

					if (RegionHadEvents)
					{
						CurDepth++;
					}
				});
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

const TSharedPtr<const ITimingEvent> FTimingRegionsTrack::SearchEvent(
	const FTimingEventSearchParameters& InSearchParameters) const
{
	TSharedPtr<const ITimingEvent> FoundEvent;

	FindRegionEvent(InSearchParameters,
		[this, &FoundEvent]
		(double InFoundStartTime, double InFoundEndTime, uint32 InFoundDepth, const TraceServices::FTimeRegion& InRegion)
		{
			check(InRegion.Timer);
			FoundEvent = MakeShared<FTimingEvent>(SharedThis(this), InFoundStartTime, InFoundEndTime, InFoundDepth, reinterpret_cast<uint64>(InRegion.Timer->Name));
		});

	return FoundEvent;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool FTimingRegionsTrack::FindRegionEvent(
	const FTimingEventSearchParameters& InParameters,
	TFunctionRef<void(double, double, uint32, const TraceServices::FTimeRegion&)> InFoundPredicate) const
{
	// If the query start time is larger than the end of the session return false.
	{
		TSharedPtr<const TraceServices::IAnalysisSession> Session = FInsightsManager::Get()->GetSession();
		TraceServices::FAnalysisSessionReadScope SessionReadScope(*Session.Get());
		if (Session.IsValid() && InParameters.StartTime > Session->GetDurationSeconds())
		{
			return false;
		}
	}

	FFilterContext FilterConfiguratorContext;
	FilterConfiguratorContext.SetReturnValueForUnsetFilters(false);
	FilterConfiguratorContext.AddFilterData<double>(static_cast<int32>(EFilterField::StartTime), 0.0f);
	FilterConfiguratorContext.AddFilterData<double>(static_cast<int32>(EFilterField::EndTime), 0.0f);
	FilterConfiguratorContext.AddFilterData<double>(static_cast<int32>(EFilterField::Duration), 0.0f);
	FilterConfiguratorContext.AddFilterData<FString>(static_cast<int32>(EFilterField::TrackName), this->GetName());
	FilterConfiguratorContext.AddFilterData<FString>(static_cast<int32>(EFilterField::RegionName), "");
	FilterConfiguratorContext.AddFilterData<FString>(static_cast<int32>(EFilterField::RegionCategory), "");

	return TTimingEventSearch<TraceServices::FTimeRegion>::Search(

		InParameters,

		// Search Predicate
		[this]
		(TTimingEventSearch<TraceServices::FTimeRegion>::FContext& InContext)
		{
			TSharedPtr<const TraceServices::IAnalysisSession> Session = FInsightsManager::Get()->GetSession();
			const TraceServices::IRegionProvider& RegionProvider = TraceServices::ReadRegionProvider(*Session);
			TraceServices::FProviderReadScopeLock RegionProviderScopedLock(RegionProvider);

			const TraceServices::IRegionTimeline* Timeline = RegionProvider.GetTimelineForCategory(RegionsCategory);
			check(Timeline)

			auto Callback =
				[&InContext]
				(const TraceServices::FTimeRegion& Region)
				{
					InContext.Check(Region.BeginTime, Region.EndTime, Region.Depth, Region);

					if (!InContext.ShouldContinueSearching())
					{
						return false;
					}

					return true;
				};

			if (InContext.GetParameters().SearchDirection == FTimingEventSearchParameters::ESearchDirection::Forward)
			{
				Timeline->EnumerateRegions(InContext.GetParameters().StartTime, InContext.GetParameters().EndTime, Callback);
			}
			else
			{
				Timeline->EnumerateRegionsBackwards(InContext.GetParameters().EndTime, InContext.GetParameters().StartTime, Callback);
			}
		},

		// Filter Predicate
		[&FilterConfiguratorContext, &InParameters]
		(double EventStartTime, double EventEndTime, uint32 EventDepth, const TraceServices::FTimeRegion& Region)
		{
			if (!InParameters.FilterExecutor.IsValid())
			{
				return true;
			}

			TSharedPtr<const TraceServices::IAnalysisSession> Session = FInsightsManager::Get()->GetSession();
			if (!Session.IsValid())
			{
				// Region.Timer->Name and Region.Category strings are only available during a valid session.
				return false;
			}

			check(Region.Timer);
			FilterConfiguratorContext.SetFilterData<double>(static_cast<int32>(EFilterField::StartTime), EventStartTime);
			FilterConfiguratorContext.SetFilterData<double>(static_cast<int32>(EFilterField::EndTime), EventEndTime);
			FilterConfiguratorContext.SetFilterData<double>(static_cast<int32>(EFilterField::Duration), EventEndTime - EventStartTime);
			FilterConfiguratorContext.SetFilterData<FString>(static_cast<int32>(EFilterField::RegionName), Region.Timer->Name);
			FilterConfiguratorContext.SetFilterData<FString>(static_cast<int32>(EFilterField::RegionCategory), Region.Timer->Category ? Region.Timer->Category->Name : nullptr);
			return InParameters.FilterExecutor->ApplyFilters(FilterConfiguratorContext);
		},

		// Found Predicate
		InFoundPredicate,

		// Payload Matched Predicate
		TTimingEventSearch<TraceServices::FTimeRegion>::NoMatch

	); // Search
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimingRegionsTrack::SetFilterConfigurator(TSharedPtr<FFilterConfigurator> InFilterConfigurator)
{
	if (FilterConfigurator != InFilterConfigurator)
	{
		FilterConfigurator = InFilterConfigurator;
		SetDirtyFlag();
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool FTimingRegionsTrack::HasCustomFilter() const
{
	return FilterConfigurator.IsValid() && !FilterConfigurator->IsEmpty();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimingRegionsTrack::OnClipboardCopyEvent(const ITimingEvent& InSelectedEvent) const
{
	if (InSelectedEvent.CheckTrack(this) && InSelectedEvent.Is<FTimingEvent>())
	{
		const FTimingEvent& TrackEvent = InSelectedEvent.As<FTimingEvent>();

		// The pointer should be safe to access because it is stored in the Session string store.
		FString EventName(reinterpret_cast<const TCHAR*>(TrackEvent.GetType()));
		FTimingEventsTrackDrawStateBuilder::AppendDurationToEventName(EventName, TrackEvent.GetDuration());

		FPlatformApplicationMisc::ClipboardCopy(*EventName);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace UE::Insights::TimingProfiler

#undef LOCTEXT_NAMESPACE

// Copyright Epic Games, Inc. All Rights Reserved.

#include "TimingGraphSeries.h"

#define LOCTEXT_NAMESPACE "UE::Insights::FTimingGraphSeries"

namespace UE::Insights::TimingProfiler
{

INSIGHTS_IMPLEMENT_RTTI(FTimingGraphSeries)

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimingGraphSeries::SetVisibility(bool bOnOff)
{
	FGraphSeries::SetVisibility(bOnOff);

	VisibilityChangedDelegate.Broadcast(bOnOff);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace UE::Insights::TimingProfiler

#undef LOCTEXT_NAMESPACE

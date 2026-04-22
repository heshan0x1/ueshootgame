// Copyright Epic Games, Inc. All Rights Reserved.

#include "Debugger/StateTreeRewindDebuggerExtensions.h"

#if WITH_STATETREE_TRACE_DEBUGGER
#include "Debugger/StateTreeTraceTypes.h"
#include "StateTreeDelegates.h"
#include "IRewindDebugger.h"

namespace UE::StateTreeDebugger
{

//----------------------------------------------------------------------//
// FRewindDebuggerPlaybackExtension
//----------------------------------------------------------------------//
void FRewindDebuggerPlaybackExtension::Update(float DeltaTime, IRewindDebugger* RewindDebugger)
{
	const TraceServices::IAnalysisSession* AnalysisSession = RewindDebugger->GetAnalysisSession();
	TraceServices::FAnalysisSessionReadScope SessionReadScope(*AnalysisSession);

	const TraceServices::IFrameProvider& FrameProvider = TraceServices::ReadFrameProvider(*RewindDebugger->GetAnalysisSession());
	TraceServices::FFrame Frame;

	// Require some debug frame to exist before doing any processing, currently the frame itself is not used
	if (FrameProvider.GetFrameFromTime(TraceFrameType_Game, RewindDebugger->CurrentTraceTime(), Frame))
	{
		auto SetScrubberTimeline = [&]()
		{
			if (RewindDebugger->IsPIESimulating())
			{
				return;
			}

			const double CurrentScrubTime = RewindDebugger->GetScrubTime();
			if (LastScrubTime != CurrentScrubTime)
			{
				StateTree::Delegates::OnTracingTimelineScrubbed.Broadcast(CurrentScrubTime);
				LastScrubTime = CurrentScrubTime;
			}
		};
		SetScrubberTimeline();
	}
}

void FRewindDebuggerPlaybackExtension::RecordingStarted(IRewindDebugger* RewindDebugger)
{
	StateTree::Delegates::OnTraceAnalysisStateChanged.Broadcast(EStateTreeTraceAnalysisStatus::Started);
}

void FRewindDebuggerPlaybackExtension::RecordingStopped(IRewindDebugger* RewindDebugger)
{
	StateTree::Delegates::OnTraceAnalysisStateChanged.Broadcast(EStateTreeTraceAnalysisStatus::Stopped);
}

void FRewindDebuggerPlaybackExtension::Clear(IRewindDebugger* RewindDebugger)
{
	StateTree::Delegates::OnTraceAnalysisStateChanged.Broadcast(EStateTreeTraceAnalysisStatus::Cleared);
}

} // UE::StateTreeDebugger

#endif // WITH_STATETREE_TRACE_DEBUGGER

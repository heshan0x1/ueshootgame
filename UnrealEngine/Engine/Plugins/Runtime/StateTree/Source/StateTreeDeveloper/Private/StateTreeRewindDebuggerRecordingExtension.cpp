// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_STATETREE_TRACE
#include "StateTreeRewindDebuggerRecordingExtension.h"
#include "Debugger/StateTreeTraceTypes.h"
#include "StateTreeDelegates.h"
#include "Debugger/StateTreeTrace.h"

namespace UE::StateTreeDebugger
{

//----------------------------------------------------------------------//
// FRewindDebuggerRecordingExtension
//----------------------------------------------------------------------//
void FRewindDebuggerRecordingExtension::RecordingStarted()
{
	if (!UE_TRACE_CHANNELEXPR_IS_ENABLED(StateTreeDebugChannel))
	{
		Trace::ToggleChannel(TEXT("StateTreeDebugChannel"), true);
		StateTree::Delegates::OnTracingStateChanged.Broadcast(EStateTreeTraceStatus::TracesStarted);
	}
}

void FRewindDebuggerRecordingExtension::RecordingStopped()
{
	// Shouldn't fire as channel will already be disabled when rewind debugger stops, but just as safeguard
	if (UE_TRACE_CHANNELEXPR_IS_ENABLED(StateTreeDebugChannel))
	{
		StateTree::Delegates::OnTracingStateChanged.Broadcast(EStateTreeTraceStatus::StoppingTrace);
		Trace::ToggleChannel(TEXT("StateTreeDebugChannel"), false);
	}
}

void FRewindDebuggerRecordingExtension::Clear()
{
	StateTree::Delegates::OnTracingStateChanged.Broadcast(EStateTreeTraceStatus::Cleared);
}

} // UE::StateTreeDebugger

#endif // WITH_STATETREE_TRACE
// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#if WITH_STATETREE_TRACE
#include "RewindDebuggerRuntimeInterface/IRewindDebuggerRuntimeExtension.h"

namespace UE::StateTreeDebugger
{

class FRewindDebuggerRecordingExtension final : public IRewindDebuggerRuntimeExtension
{
	virtual void RecordingStarted() override;
	virtual void RecordingStopped() override;
	virtual void Clear() override;
};

} // UE::StateTreeDebugger

#endif // WITH_STATETREE_TRACE
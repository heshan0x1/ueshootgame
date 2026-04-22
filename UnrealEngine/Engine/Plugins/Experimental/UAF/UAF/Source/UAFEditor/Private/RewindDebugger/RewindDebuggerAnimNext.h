// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "RewindDebugger/AnimNextTrace.h"

#if ANIMNEXT_TRACE_ENABLED
#include "CoreMinimal.h"
#include "IRewindDebugger.h"
#include "IRewindDebuggerExtension.h"

// Rewind debugger extension for Chooser support

class FRewindDebuggerAnimNext : public IRewindDebuggerExtension
{
public:
	FRewindDebuggerAnimNext();
	virtual ~FRewindDebuggerAnimNext() {};

	virtual void Update(float DeltaTime, IRewindDebugger* RewindDebugger) override;
};
#endif //ANIMNEXT_TRACE_ENABLED

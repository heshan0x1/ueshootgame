// Copyright Epic Games, Inc. All Rights Reserved.

#include "RewindDebuggerAnimNext.h"

#if ANIMNEXT_TRACE_ENABLED
#include "IGameplayProvider.h"

FRewindDebuggerAnimNext::FRewindDebuggerAnimNext()
{

}

void FRewindDebuggerAnimNext::Update(float DeltaTime, IRewindDebugger* RewindDebugger)
{
	// Future place to sync anim next workspace editor debugging when scrubbing
}

#endif // ANIMNEXT_TRACE_ENABLED

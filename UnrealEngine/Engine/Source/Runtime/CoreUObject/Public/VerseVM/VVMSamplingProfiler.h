// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_VERSE_VM || defined(__INTELLISENSE__)

#include "Async/EventCount.h"
#include "Containers/Set.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "Templates/UniquePtr.h"
#include "VerseVM/VVMContext.h"
#include "VerseVM/VVMFrame.h"
#include "VerseVM/VVMNativeFunction.h"
#include "VerseVM/VVMTask.h"

#include "Trace/Config.h"
#include "Trace/Detail/Channel.h"
#include "Trace/Trace.h"

namespace Verse
{
// Enabled via cmd-line arg `-trace=verse` passed to the program being sampled
UE_TRACE_MINIMAL_CHANNEL_EXTERN(VerseChannel, COREUOBJECT_API);

struct FSampledFrame
{
	TWriteBarrier<VTask> Task;
	TWriteBarrier<VFrame> Frame;
	bool bIsNativeCall{false};
	TArray<TWriteBarrier<VUniqueString>> NativeFrameCallstack;
	uint32 BytecodeOffset;
	uint64 Cycles;
	void MarkReferencedCells(FMarkStack& MarkStack)
	{
		MarkStack.MarkNonNull(Task.Get());
		MarkStack.MarkNonNull(Frame.Get());
		for (uint32 Index = 0; Index < NativeFrameCallstack.Num(); ++Index)
		{
			MarkStack.MarkNonNull(NativeFrameCallstack[Index].Get());
		}
	}
};

struct FVerseSample
{
	uint32 Hits = 0u;
	TArray<VUniqueString*> Callstack;
	TArray<TPair</*StartCycle*/ uint64, /*ApproxCycles*/ uint64>> Cycles;
	TMap<TPair</*bytecodeoffset*/ uint32, /*line*/ uint32>, uint32> BytecodeHits;
	bool operator==(const FVerseSample& Other) const
	{
		return Callstack == Other.Callstack;
	}
};

inline uint32 GetTypeHash(const FVerseSample& Sample)
{
	return GetTypeHash(Sample.Callstack);
}

// - TODO: No longer assume a single mutator. Maybe this is OK in a UEFN context, but it quickly becomes
//   not OK if we end up running the VM with multiple mutators
// - TODO: Redact non-user visible callstacks
struct FSamplingProfiler final : FRunnable
{
	COREUOBJECT_API void Start();
	void Pause() { bPauseRequested.store(true, std::memory_order_seq_cst); }

	FSamplingProfiler() { Start(); };
	void SetMutatorContext(FRunningContext* Context) { MutatorContext = Context; }

	// FRunnable Interface
	COREUOBJECT_API uint32 Run() override;
	void Stop() override { bStopRequested.store(true, std::memory_order_seq_cst); }
	COREUOBJECT_API void Exit() override;
	// ~FRunnable Interface

	void Sample(FRunningContext Context, FOp* PC, VFrame* Frame, VTask* Task);

	// Non GC calls to process samples need to lock the GC mutex first
	void ProcessSamples(TArray<FSampledFrame>& Samples);

	void MarkReferencedCells(FMarkStack&);

	// This only work in debug builds atm... reason why is not clear.
	void Dump(uint32 MaxFuncPrints = 10, uint32 MaxCallstackPrints = 3, uint32 MaxBytecodePrints = 5);

	FRunningContext* MutatorContext{nullptr}; // We need this to PairHandshake

	UE::FMutex GCMutex;
	UE::FMutex ProcessingMutex;

	TArray<FSampledFrame> Samples;

	// We cache these so we can re-use the processed callstacks from them
	// when sampling. They are removed in FNativeFrame's dtor
	TMap<const FNativeFrame*, TArray<TWriteBarrier<VUniqueString>>> CachedNativeFrameCallstacks;

	TUniquePtr<FRunnableThread> Thread;

	bool bIsRunning = false;
	std::atomic<bool> bStopRequested = false;
	std::atomic<bool> bPauseRequested = false;
	UE::FEventCount WaitEvent;

	// Used to trace the strings already emitted to insights this session
	// TODO: Reset these upon a new insights connection
	uint32 StringIdCounter = 0;
	TMap<VUniqueString*, uint32> TracedStringIds;

	TSet<FVerseSample> LogSamples;
	TPair</*StartCycle*/ uint64, /*ApproxCycles*/ uint64>* PreviousSampleTimeEntry{nullptr};
};

COREUOBJECT_API FSamplingProfiler* GetSamplingProfiler();
COREUOBJECT_API FSamplingProfiler* GetRunningSamplingProfiler();
COREUOBJECT_API void SetSamplingProfiler(FSamplingProfiler*);

} // namespace Verse

#endif

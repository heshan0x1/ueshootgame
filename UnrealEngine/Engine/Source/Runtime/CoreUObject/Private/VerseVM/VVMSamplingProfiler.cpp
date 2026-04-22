// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_VERSE_VM || defined(__INTELLISENSE__)

#include "VerseVM/VVMSamplingProfiler.h"

#include "Async/UniqueLock.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Fork.h"
#include "VerseVM/VVMLog.h"

#include "Trace/Trace.inl"

static bool bLogVerseVMSampling = false;
static FAutoConsoleVariableRef CVarLogVerseVMSampling(TEXT("sol.LogVerseVMSampling"), bLogVerseVMSampling, TEXT("If `true` the Verse VM Sampler will store and UE-Log its result via `Dump()` calls."));

namespace Verse
{

UE_TRACE_MINIMAL_CHANNEL_DEFINE(VerseChannel);

UE_TRACE_MINIMAL_EVENT_BEGIN(Verse, DeclareString)
UE_TRACE_MINIMAL_EVENT_FIELD(uint32, Id)
UE_TRACE_MINIMAL_EVENT_FIELD(UE::Trace::WideString, Name)
UE_TRACE_MINIMAL_EVENT_END()

UE_TRACE_MINIMAL_EVENT_BEGIN(Verse, BytecodeSample)
UE_TRACE_MINIMAL_EVENT_FIELD(uint64, Cycles)
UE_TRACE_MINIMAL_EVENT_FIELD(uint32, BytecodeOffset)
UE_TRACE_MINIMAL_EVENT_FIELD(uint32, Line)
UE_TRACE_MINIMAL_EVENT_FIELD(uint32[], Callstack)
UE_TRACE_MINIMAL_EVENT_END()

UE_TRACE_MINIMAL_EVENT_BEGIN(Verse, NativeSample)
UE_TRACE_MINIMAL_EVENT_FIELD(uint64, Cycles)
UE_TRACE_MINIMAL_EVENT_FIELD(uint32[], Callstack)
UE_TRACE_MINIMAL_EVENT_END()

static FSamplingProfiler* GSamplingProfiler = nullptr;

FSamplingProfiler* GetSamplingProfiler()
{
	return GSamplingProfiler;
}

FSamplingProfiler* GetRunningSamplingProfiler()
{
	return GSamplingProfiler && GSamplingProfiler->bIsRunning ? GSamplingProfiler : nullptr;
}

void SetSamplingProfiler(FSamplingProfiler* SamplingProfiler)
{
	V_DIE_IF(GSamplingProfiler);
	V_DIE_UNLESS(SamplingProfiler);
	StoreStoreFence();
	GSamplingProfiler = SamplingProfiler;
}

void FSamplingProfiler::Start()
{
	if (!Thread)
	{
		// Make forkable as otherwise we don't actually run on UEFN's single-threaded servers
		Thread = TUniquePtr<FRunnableThread>(FForkProcessHelper::CreateForkableThread(this, TEXT("Verse VM Sampling Profiler")));
	}

	bPauseRequested.store(false, std::memory_order_seq_cst);
	bStopRequested.store(false, std::memory_order_seq_cst);
	WaitEvent.Notify();
}

uint32 FSamplingProfiler::Run()
{
	FIOContextScope ContextScope;
	bIsRunning = true;
	while (true)
	{
		// TODO: Make the VerseVM request a pause upon all of its mutator threads exiting / start again when a new one is created
		UE::FEventCountToken Token = WaitEvent.PrepareWait();
		if (bPauseRequested)
		{
			WaitEvent.Wait(Token);
			continue;
		}

		if (bStopRequested)
		{
			return 0;
		}

		FPlatformProcess::Sleep(1.0f / 1000.0f);
		if (MutatorContext)
		{
			// By requesting a handshake we will trigger the slowpath of 'CheckForHandshake' in the interpreter which will sample.
			// This is better than using a state-bit as we did before as it avoids us always taking the handshake slowpath when sampling.
			//
			// Note: It may be nice to rework the pair-handshake API to pass the current interpreter state to the caller so the callsite
			//		 can define what to do rather than the CheckForHandshake() lambda as we have now.
			ContextScope.Context.PairHandshake(*MutatorContext, [](FHandshakeContext HandshakeContext) {});
		}
	}
	bIsRunning = false;

	return 0;
}

void FSamplingProfiler::Exit()
{
	Thread->WaitForCompletion();
	Thread = nullptr;

	bStopRequested.store(false, std::memory_order_seq_cst);
	bPauseRequested.store(false, std::memory_order_seq_cst);
}

// Is this logic sufficient for suspended calls?
void FSamplingProfiler::Sample(FRunningContext Context, FOp* PC, VFrame* Frame, VTask* Task)
{
	if (!Frame->Procedure)
	{
		return;
	}

	const FNativeFrame* NativeFrame = Context.NativeFrame();
	TArray<TWriteBarrier<VUniqueString>>* Cache = CachedNativeFrameCallstacks.Find(NativeFrame);
	if (!Cache)
	{
		TArray<TWriteBarrier<VUniqueString>> Callstack;
		NativeFrame->WalkTaskFrames(Task, [&Callstack](const FNativeFrame& Frame) {
			if (const VNativeFunction* Callee = Frame.Callee)
			{
				Callstack.Add(Callee->Name);
			}
			const VFrame* CallerFrame = Frame.CallerFrame;
			while (CallerFrame)
			{
				Callstack.Add(CallerFrame->Procedure->Name);
				CallerFrame = CallerFrame->CallerFrame.Get();
			}
		});
		Cache = &CachedNativeFrameCallstacks.Add({NativeFrame, TArray<TWriteBarrier<VUniqueString>>(MoveTemp(Callstack))});
	}

	UE::TUniqueLock Lock(GCMutex);
	Samples.Add(FSampledFrame{
		.Task = {Context,  Task},
		.Frame = {Context, Frame},
		.bIsNativeCall = !!NativeFrame->Callee,
		.NativeFrameCallstack = *Cache,
		.BytecodeOffset = Frame->Procedure->BytecodeOffset(*PC),
		.Cycles = FPlatformTime::Cycles64()
    });
}

void FSamplingProfiler::MarkReferencedCells(FMarkStack& MarkStack)
{
	UE::TUniqueLock Lock(GCMutex);
	for (uint32 Index = 0; Index < Samples.Num(); ++Index)
	{
		Samples[Index].MarkReferencedCells(MarkStack);
	}
	for (auto& Pair : CachedNativeFrameCallstacks)
	{
		for (uint32 Index = 0; Index < Pair.Value.Num(); ++Index)
		{
			MarkStack.MarkNonNull(Pair.Value[Index].Get());
		}
	}
	for (auto& Pair : TracedStringIds)
	{
		MarkStack.MarkNonNull(Pair.Key);
	}

	if (CVarLogVerseVMSampling->GetBool())
	{
		// this is allot of re-marking, but seems simpler than trying to tie into the insights data-structures
		// which don't need to store the strings for the entire sampling run
		for (FVerseSample& LogSample : LogSamples)
		{
			for (uint32 Index = 0; Index < LogSample.Callstack.Num(); ++Index)
			{
				MarkStack.MarkNonNull(LogSample.Callstack[Index]);
			}
		}
	}
}

void FSamplingProfiler::ProcessSamples(TArray<FSampledFrame>& InSamples)
{
	auto GetAndTraceString = [this](VUniqueString* String) -> uint32 {
		if (uint32* Result = TracedStringIds.Find(String))
		{
			return *Result;
		}
		UE_TRACE_MINIMAL_LOG(Verse, DeclareString, VerseChannel)
			<< DeclareString.Id(++StringIdCounter)
			<< DeclareString.Name(*String->AsString());
		return TracedStringIds.Add(String, StringIdCounter);
	};

	auto BuildCallstack = []<typename Type, typename GetAndTraceString>(FSampledFrame& Sample, TArray<Type>& OutCallstack, GetAndTraceString Callback) {
		if (!Sample.bIsNativeCall)
		{
			VFrame* Frame = Sample.Frame.Get();
			while (Frame)
			{
				OutCallstack.Add(Callback(Frame->Procedure->Name.Get()));
				Frame = Frame->CallerFrame.Get();
			}
		}

		for (uint32 Index = 0; Index < Sample.NativeFrameCallstack.Num(); ++Index)
		{
			OutCallstack.Add(Callback(Sample.NativeFrameCallstack[Index].Get()));
		}
	};

	for (FSampledFrame& Sample : InSamples)
	{
		// Insight's Events
		TArray<uint32> CallstackIds;
		BuildCallstack(Sample, CallstackIds, GetAndTraceString);

		const FLocation* Location = Sample.Frame->Procedure->GetLocation(Sample.BytecodeOffset);
		if (Sample.bIsNativeCall)
		{
			UE_TRACE_MINIMAL_LOG(Verse, NativeSample, VerseChannel)
				<< NativeSample.Cycles(Sample.Cycles)
				<< NativeSample.Callstack(CallstackIds.GetData(), CallstackIds.Num());
		}
		else
		{
			UE_TRACE_MINIMAL_LOG(Verse, BytecodeSample, VerseChannel)
				<< BytecodeSample.Cycles(Sample.Cycles)
				<< BytecodeSample.BytecodeOffset(Sample.BytecodeOffset)
				<< BytecodeSample.Line(Location ? Location->Line : 0u)
				<< BytecodeSample.Callstack(CallstackIds.GetData(), CallstackIds.Num());
		}

		// Local logging
		if (CVarLogVerseVMSampling->GetBool())
		{
			TArray<VUniqueString*> CallstackStrings;
			BuildCallstack(Sample, CallstackStrings, [](VUniqueString* String) -> VUniqueString* { return String; });

			FVerseSample& LogSample = LogSamples.FindOrAdd({.Callstack = CallstackStrings});
			++LogSample.Hits;
			LogSample.Cycles.Add({Sample.Cycles, 0u});
			if (PreviousSampleTimeEntry)
			{
				PreviousSampleTimeEntry->Value = Sample.Cycles - PreviousSampleTimeEntry->Key;
			}
			PreviousSampleTimeEntry = &LogSample.Cycles.Last();

			if (!Sample.bIsNativeCall)
			{
				++LogSample.BytecodeHits.FindOrAdd({Sample.BytecodeOffset, Location ? Location->Line : 0u}, 0u);
			}
		}
	}
}

void FSamplingProfiler::Dump(uint32 MaxFuncPrints, uint32 MaxCallstackPrints, uint32 MaxBytecodePrints)
{
	if (CVarLogVerseVMSampling->GetBool())
	{
		UE::TUniqueLock Lock(ProcessingMutex);
		{
			UE::TUniqueLock GCLock(GCMutex);
			TArray<FSampledFrame> SamplesToProcess = MoveTemp(Samples);
			ProcessSamples(SamplesToProcess);
		}

		uint64 TotalHits = 0;
		uint64 TotalCycles = 0;
		TMap<VUniqueString*, uint32> FuncsToHits;
		TMap<VUniqueString*, uint64> FuncsToCycles;
		TMap<VUniqueString*, TSet<FVerseSample*>> FuncsToSamples;
		for (FVerseSample& Sample : LogSamples)
		{
			VUniqueString* Name = Sample.Callstack[0];
			FuncsToHits.FindOrAdd(Name, 0u) += Sample.Hits;
			FuncsToSamples.FindOrAdd(Name, {}).Add(&Sample);
			for (auto& Pair : Sample.Cycles)
			{
				FuncsToCycles.FindOrAdd(Name, 0u) += Pair.Value;
			}

			TotalHits += Sample.Hits;
			TotalCycles += FuncsToCycles.FindChecked(Name);
		}

		FuncsToHits.ValueSort([](const uint32& A, const uint32& B) {
			return A > B;
		});

		uint32 NumFuncs = 0;
		UE_LOG(LogVerseVM, Display, TEXT("\n"));
		UE_LOG(LogVerseVM, Display, TEXT("----------------------------------\n"));
		UE_LOG(LogVerseVM, Display, TEXT("Top Functions (TotalHits=%u TotalCycles=%u)\n"), TotalHits, TotalCycles);
		for (TPair<VUniqueString*, uint32>& Pair : FuncsToHits)
		{
			uint32 NumCallstacks = 0;
			uint32 NumByteCodes = 0;

			uint64 Cycles = FuncsToCycles.FindChecked(Pair.Key);
			UE_LOG(LogVerseVM, Display, TEXT("%s Hits=%u (%.2f%%) Cycles=%u (%.2f%%)"),
				*Pair.Key->AsString(),
				Pair.Value,
				((double)Pair.Value / (double)TotalHits) * 100.00,
				Cycles,
				((double)Cycles / (double)TotalCycles) * 100.00);

			UE_LOG(LogVerseVM, Display, TEXT("	Top Callstacks:"));
			TSet<FVerseSample*> FuncSamples = FuncsToSamples.FindChecked(Pair.Key);
			FuncSamples.Sort([](const FVerseSample A, const FVerseSample B) {
				return A.Hits > B.Hits;
			});
			uint32 CallstackNum = 1;
			for (FVerseSample* FuncSample : FuncSamples)
			{
				bool bFirst = true;
				for (VUniqueString* Call : FuncSample->Callstack)
				{
					if (bFirst)
					{
						UE_LOG(LogVerseVM, Display, TEXT("	%s Hits=%u (%.2f%%)"),
							*Call->AsString(),
							FuncSample->Hits,
							((double)FuncSample->Hits / (double)Pair.Value) * 100.00);
					}
					else
					{
						UE_LOG(LogVerseVM, Display, TEXT("	%s"), *Call->AsString());
					}
					bFirst = false;
				}
				if (++NumCallstacks >= MaxCallstackPrints)
				{
					UE_LOG(LogVerseVM, Display, TEXT("	..."));
					break;
				}
				UE_LOG(LogVerseVM, Display, TEXT(""));
			}

			TMap<TPair</*bytecodeoffset*/ uint32, /*line*/ uint32>, uint32> BytecodeHits;
			for (FVerseSample* FuncSample : FuncSamples)
			{
				for (auto& BytecodePair : FuncSample->BytecodeHits)
				{
					BytecodeHits.FindOrAdd(BytecodePair.Key, 0u) += BytecodePair.Value;
				}
			}
			BytecodeHits.ValueSort([](const uint32& A, const uint32& B) {
				return A > B;
			});

			UE_LOG(LogVerseVM, Display, TEXT("	Top Bytecodes:"));
			for (auto& BytecodePair : BytecodeHits)
			{
				UE_LOG(LogVerseVM, Display, TEXT("	bc#%u/line#%u Hits=%u"), BytecodePair.Key.Key, BytecodePair.Key.Value, BytecodePair.Value);
				if (++NumByteCodes >= MaxBytecodePrints)
				{
					UE_LOG(LogVerseVM, Display, TEXT("	..."));
					break;
				}
			}

			if (++NumFuncs >= MaxFuncPrints)
			{
				UE_LOG(LogVerseVM, Display, TEXT("..."));
				break;
			}
		}
		UE_LOG(LogVerseVM, Display, TEXT("\n"));
		UE_LOG(LogVerseVM, Display, TEXT("----------------------------------\n"));
	}
}

} // namespace Verse

#endif // WITH_VERSE_VM || defined(__INTELLISENSE__)

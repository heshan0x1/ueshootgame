// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_VERSE_VM || defined(__INTELLISENSE__)

#include "Misc/ScopeExit.h"
#include "VerseVM/VVMBytecodesAndCaptures.h"
#include "VerseVM/VVMContentScope.h"
#include "VerseVM/VVMContext.h"
#include "VerseVM/VVMFailureContext.h"
#include "VerseVM/VVMFrame.h"
#include "VerseVM/VVMSamplingProfiler.h"
#include "VerseVM/VVMTask.h"

namespace Verse
{
// The entry point that all `C++` calls into Verse should go through
//
// Style note: Try not to split this function out into other calls too much...
// we want anyone who reads this to easily be able to grasp the logic-flow.
template <typename TFunctor>
void FRunningContext::EnterVM_Internal(EEnterVMMode Mode, TFunctor F)
{
	AutoRTFM::UnreachableIfClosed("#jira SOL-8415");

	FContextImpl* Impl = GetImpl();

	// TODO: ideally, we would always have an active content scope here and wouldn't need to check `IsActive`.
	// At present, omitting `IsActive` breaks many tests, as well as FVerseVmAssembler::TranslateExpression.
	// #jira SOL-8492
	VTaskGroup* PreviousTaskGroup = Impl->CurrentTaskGroup();
	if (verse::FContentScopeGuard::IsActive())
	{
		const TSharedRef<verse::FContentScope>& Scope = verse::FContentScopeGuard::GetActiveScope();
		if (!Scope->ShouldExecuteCodeWithThisScope())
		{
			// The active content scope was terminated.
			return;
		}
		Impl->SetCurrentTaskGroup(Scope->GetTaskGroup());
	}

	ON_SCOPE_EXIT
	{
		Impl->SetCurrentTaskGroup(PreviousTaskGroup);
	};

	const bool bTopLevel = Impl->_NativeFrame == nullptr;
	if (bTopLevel)
	{
		Impl->StartComputationWatchdog();
		if (FSamplingProfiler* Sampler = GetRunningSamplingProfiler())
		{
			Sampler->SetMutatorContext(this);
			Sampler->Start();
		}
	}

	auto SetupAndRun = [&]() {
		AutoRTFM::UnreachableIfClosed("#jira SOL-8415");

		COREUOBJECT_API extern FOpErr StopInterpreterSentry;
		if (bTopLevel)
		{
			// We need to create a 'root' frame as this call into the interpreter could either come
			// from the top level or from some C++ that Verse called into higher up the stack.
			// So, this frame represents that top level C++. This task is meant as a "last resort,"
			// and the interpreter is expected to create a new task in BeginTask instead of using this.
			// (However, it's valid for the interpreter to just stay in this task and never BeginTask,
			// if we're just running non-task-y code that never uses spawn/sync/race/etc.)
			VTask* Task = &VTask::New(*this, &StopInterpreterSentry, VFrame::GlobalEmptyFrame.Get(), /*YieldTask*/ nullptr, /*Parent*/ nullptr);
			VFailureContext* FailureContext = &VFailureContext::New(*this, Task, nullptr, *VFrame::GlobalEmptyFrame.Get(), VValue(), &StopInterpreterSentry);
			FNativeFrame NewNativeFrame{
				.FailureContext = FailureContext,
				.Task = Task,
				.EffectToken = VValue::EffectDoneMarker()};
			TGuardValue<FNativeFrame*> NativeFrameGuard(Impl->_NativeFrame, &NewNativeFrame);

			NewNativeFrame.Start(*this);
			F();
			NewNativeFrame.CommitIfNoAbort(*this);
		}
		else if (Mode == EEnterVMMode::NewTransaction)
		{
			const FNativeFrame* NativeFrame = GetImpl()->NativeFrame();
			VFailureContext* NewFailureContext = &VFailureContext::New(*this, NativeFrame->Task, NativeFrame->FailureContext, *VFrame::GlobalEmptyFrame.Get(), VValue(), &StopInterpreterSentry);
			TGuardValue<VFailureContext*> FailureContextGuard(Impl->_NativeFrame->FailureContext, NewFailureContext);

			Impl->_NativeFrame->Start(*this);
			F();
			Impl->_NativeFrame->CommitIfNoAbort(*this);
		}
		else
		{
			F();
		}
	};

	if (AutoRTFM::IsTransactional())
	{
		SetupAndRun();
	}
	else
	{
		// `NativeContext.Start` calls `AutoRTFM::ForTheRuntime::StartTransaction()`
		// which can't start a new transaction stack, so we need to make one first.
		AutoRTFM::TransactThenOpen([&] { SetupAndRun(); });
	}

	if (bTopLevel)
	{
		Impl->PauseComputationWatchdog();
		if (FSamplingProfiler* Sampler = GetRunningSamplingProfiler())
		{
			Sampler->Pause();
			Sampler->SetMutatorContext(nullptr);
		}
	}
}

} // namespace Verse
#endif // WITH_VERSE_VM

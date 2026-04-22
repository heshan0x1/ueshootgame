// Copyright Epic Games, Inc. All Rights Reserved.

#include "VerseVM/VVMRuntimeError.h"
#include "AutoRTFM.h"
#include "Blueprint/BlueprintExceptionInfo.h"
#include "HAL/Platform.h"
#include "Misc/StringBuilder.h"
#include "UObject/Script.h"
#include "UObject/Stack.h"
#include "VerseVM/VVMCVars.h"
#include "VerseVM/VVMContentScope.h"
#include "VerseVM/VVMContext.h"
#include "VerseVM/VVMFailureContext.h"

DEFINE_LOG_CATEGORY(LogVerseRuntimeError);

namespace Verse
{
constexpr SRuntimeDiagnosticInfo DiagnosticInfos[] = {
#define VISIT_DIAGNOSTIC(_Severity, _EnumName, _Description) {.Name = #_EnumName, .Description = _Description, .Severity = ERuntimeDiagnosticSeverity::_Severity},
	VERSE_RUNTIME_GLITCH_ENUM_DIAGNOSTICS(VISIT_DIAGNOSTIC)
#undef VISIT_DIAGNOSTIC
};

const SRuntimeDiagnosticInfo& GetRuntimeDiagnosticInfo(const ERuntimeDiagnostic Diagnostic)
{
	if (ensureMsgf(size_t(Diagnostic) < UE_ARRAY_COUNT(DiagnosticInfos), TEXT("Invalid runtime diagnostic enum: %zu"), static_cast<size_t>(Diagnostic)))
	{
		return DiagnosticInfos[size_t(Diagnostic)];
	}
	// Just return that an unknown internal error occured if the code can't be found
	return DiagnosticInfos[size_t(ERuntimeDiagnostic::ErrRuntime_Internal)];
}

FString AsFormattedString(const ERuntimeDiagnostic& Diagnostic, const FText& MessageText)
{
	const SRuntimeDiagnosticInfo& Info = GetRuntimeDiagnosticInfo(Diagnostic);
	FStringBuilderBase Result;
	Result += TEXT("Verse ");
	switch (Info.Severity)
	{
		case ERuntimeDiagnosticSeverity::UnrecoverableError:
			Result.Append(TEXT("unrecoverable error"));
			break;
		default:
			// Unreachable code
			ensureMsgf(false, TEXT("Unsupported enum: %d!"), Info.Severity);
			break;
	}
	Result.Appendf(TEXT(": %s: %s"), ANSI_TO_TCHAR(Info.Name), ANSI_TO_TCHAR(Info.Description));
	if (!MessageText.IsEmpty())
	{
		Result.Appendf(TEXT(" (%s)"), *MessageText.ToString());
	}
	return Result.ToString();
}

#if WITH_VERSE_VM
void FContext::RaiseVerseRuntimeError(const Verse::ERuntimeDiagnostic Diagnostic, const FText& MessageText)
{
	AutoRTFM::UnreachableIfClosed("#jira SOL-8415");

	if (Verse::CVarBreakOnVerseRuntimeError->GetBool() == true && FPlatformMisc::IsDebuggerPresent())
	{
		PLATFORM_BREAK();
	}

	FRunningContext RunningContext(FRunningContextPromise{});
	if (Verse::FNativeFrame* NativeFrame = RunningContext.NativeFrame())
	{
		VFailureContext* Failure = NativeFrame->RootFailureContext();

		// Abort all the transactions owned by the VVM.
		Failure->Fail(RunningContext);

		// Cascade-abort the outer transaction. This immediately rolls back
		// the transaction, but does not immediately pop it from the AutoRTFM
		// transaction stack. Once execution reaches the open -> closed
		// boundary, this transaction is popped and all outer transactions will
		// be aborted.
		AutoRTFM::ForTheRuntime::CascadingAbortRollbackTransaction();
	}

	// After the rollback, report the runtime error and terminate the active content scope.
	if (verse::FContentScopeGuard::IsActive())
	{
		// TODO: (yiliang.siew) Eventually, this needs to _not_ be gated by whether a content scope is
		// active. However, previously (`30.40`) we didn't broadcast this unless there was an execution scope active,
		// and by doing that now, we'd end up generating runtime errors when `block`s in `creative_object` subtypes
		// run during level load, thus failing things like cooks, or even opening the project.
		FVerseRuntimeErrorDelegates::OnVerseRuntimeError.Broadcast(Diagnostic, MessageText);
		verse::FContentScopeGuard::GetActiveScope()->Terminate();
	}
}

void RaiseVerseRuntimeError(const Verse::ERuntimeDiagnostic Diagnostic, const FText& MessageText)
{
	AutoRTFM::Open([Diagnostic, Str = MessageText.BuildSourceString()] {
		FText MessageTextCopy = FText::FromString(Str);

		FRunningContext RunningContext{FRunningContextPromise{}};
		RunningContext.RaiseVerseRuntimeError(Diagnostic, MessageTextCopy);
	});
}
#else
static void DoRaiseVerseRuntimeErrorInOpen(const Verse::ERuntimeDiagnostic Diagnostic, const FText& MessageText)
{
	// This method is only to be called in the open, so let's make sure that is the case!
	AutoRTFM::UnreachableIfClosed("#jira SOL-8415");

	FVerseRuntimeErrorDelegates::OnUnwindRuntimeError.Broadcast(Diagnostic, MessageText);

	if (verse::FContentScopeGuard::IsActive())
	{
		// TODO: (yiliang.siew) Eventually, this needs to _not_ be gated by whether a content scope is
		// active. However, previously (`30.40`) we didn't broadcast this unless there was an execution scope active,
		// and by doing that now, we'd end up generating runtime errors when `block`s in `creative_object` subtypes
		// run during level load, thus failing things like cooks, or even opening the project.
		FVerseRuntimeErrorDelegates::OnVerseRuntimeError.Broadcast(Diagnostic, MessageText);
		verse::FContentScopeGuard::GetActiveScope()->Terminate();
	}

	if (FFrame* Frame = FFrame::GetThreadLocalTopStackFrame())
	{
		FBlueprintExceptionInfo ExceptionInfo(EBlueprintExceptionType::AbortExecution, MessageText);
		FBlueprintCoreDelegates::ThrowScriptException(Frame->Object, *Frame, ExceptionInfo);
	}
}

void RaiseVerseRuntimeError(const Verse::ERuntimeDiagnostic Diagnostic, const FText& MessageText)
{
	if (Verse::CVarBreakOnVerseRuntimeError->GetBool() == true && FPlatformMisc::IsDebuggerPresent())
	{
		PLATFORM_BREAK();
	}

	if (AutoRTFM::IsTransactional())
	{
		// We do not expect this function to be called from the closed, but
		// for now ensure this is true and emit a call to AutoRTFM::Close() to
		// guarantee the call to CascadingAbortTransaction() is closed.
		// Once we're confident that this is not called from the open, the
		// AutoRTFM::Close() can be replaced with the body of the lambda.
		ensure(AutoRTFM::IsClosed());

		(void)AutoRTFM::Close([&] {
			// We use a cascading abort here because when a runtime error occurs, script execution is entirely halted.
			// Runtime errors are unrecoverable.
			AutoRTFM::OnComplete([Diagnostic, Str = MessageText.BuildSourceString()] {
				FText MessageTextCopy = FText::FromString(Str);
				DoRaiseVerseRuntimeErrorInOpen(Diagnostic, MessageTextCopy);
			});
			AutoRTFM::CascadingAbortTransaction();
		});
	}
	else
	{
		DoRaiseVerseRuntimeErrorInOpen(Diagnostic, MessageText);
	}
}
#endif

} // namespace Verse

#if !WITH_VERSE_VM
FVerseUnwindRuntimeErrorHandler FVerseRuntimeErrorDelegates::OnUnwindRuntimeError;
#endif

FVerseRuntimeErrorReportHandler FVerseRuntimeErrorDelegates::OnVerseRuntimeError;
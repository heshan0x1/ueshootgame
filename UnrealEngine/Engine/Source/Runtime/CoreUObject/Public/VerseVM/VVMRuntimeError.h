// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Delegates/Delegate.h"

#define UE_API COREUOBJECT_API

UE_API DECLARE_LOG_CATEGORY_EXTERN(LogVerseRuntimeError, Log, All);

namespace Verse
{
// NOTE: (yiliang.siew) Right now, these are all errors, but in the future there could be warnings potentially.
#define VERSE_RUNTIME_GLITCH_ENUM_DIAGNOSTICS(v)                                                                                                                                                                                  \
	v(UnrecoverableError, ErrRuntime_Internal, "An internal runtime error occurred. There is no other information available.")                                                                                                    \
	v(UnrecoverableError, ErrRuntime_TransactionAbortedByLanguage, "An internal transactional runtime error occurred. There is no other information available.")                                                                  \
	v(UnrecoverableError, ErrRuntime_NativeInternal, "An internal runtime error occurred in native code that was called from Verse. There is no other information available.")                                                    \
	v(UnrecoverableError, ErrRuntime_GeneratedNativeInternal, "An internal runtime error occurred in (generated) native code that was called from Verse. There is no other information available.")                               \
	v(UnrecoverableError, ErrRuntime_InfiniteLoop, "The runtime terminated prematurely because Verse code was running in an infinite loop.")                                                                                      \
	v(UnrecoverableError, ErrRuntime_ComputationLimitExceeded, "The runtime terminated prematurely because Verse code took too long to execute within a single server tick. Try offloading heavy computation to async contexts.") \
	v(UnrecoverableError, ErrRuntime_Overflow, "Overflow encountered.")                                                                                                                                                           \
	v(UnrecoverableError, ErrRuntime_Underflow, "Underflow encountered.")                                                                                                                                                         \
	v(UnrecoverableError, ErrRuntime_FloatingPointOverflow, "Floating-point overflow encountered.")                                                                                                                               \
	v(UnrecoverableError, ErrRuntime_IntegerOverflow, "Integer overflow encountered.")                                                                                                                                            \
	v(UnrecoverableError, ErrRuntime_ErrorRequested, "A runtime error was explicitly raised from user code.")                                                                                                                     \
	v(UnrecoverableError, ErrRuntime_IntegerBoundsExceeded, "A value does not fall inside the representable range of a Verse integer.")                                                                                           \
	v(UnrecoverableError, ErrRuntime_MemoryLimitExceeded, "Exceeded memory limit(s).")                                                                                                                                            \
	v(UnrecoverableError, ErrRuntime_DivisionByZero, "Division by zero attempted.")                                                                                                                                               \
	v(UnrecoverableError, ErrRuntime_WeakMapInvalidKey, "Invalid key used to access persistent `var` `weak_map`.")                                                                                                                \
	v(UnrecoverableError, ErrRuntime_InvalidVarRead, "Attempted to read a `var` out of an invalid object.")                                                                                                                       \
	v(UnrecoverableError, ErrRuntime_InvalidVarWrite, "Attempted to write to a `var` of an invalid object.")                                                                                                                      \
	v(UnrecoverableError, ErrRuntime_InvalidFunctionCall, "Attempted to call an invalid function.")                                                                                                                               \
	v(UnrecoverableError, ErrRuntime_MathIntrinsicCallFailure, "A math intrinsic failed.")                                                                                                                                        \
	v(UnrecoverableError, ErrRuntime_InvalidArrayLength, "Invalid array length.")                                                                                                                                                 \
	v(UnrecoverableError, ErrRuntime_InvalidStringLength, "Invalid string length.")                                                                                                                                               \
	v(UnrecoverableError, ErrRuntime_InvalidLeaderboard, "Leaderboard failed to compile and cannot be instantiated.")                                                                                                             \
	v(UnrecoverableError, ErrRuntime_FailedToRegisterLeaderboard, "Leaderboard could not be registered with the back end.")                                                                                                       \
	v(UnrecoverableError, ErrRuntime_UnregisteredLeaderboard, "Trying to interact with an unregistered Leaderboard.")

enum class ERuntimeDiagnostic : uint16_t
{
#define VISIT_DIAGNOSTIC(Severity, EnumName, Description) EnumName,
	VERSE_RUNTIME_GLITCH_ENUM_DIAGNOSTICS(VISIT_DIAGNOSTIC)
#undef VISIT_DIAGNOSTIC
};

enum class ERuntimeDiagnosticSeverity : uint8_t
{
	UnrecoverableError
};

struct SRuntimeDiagnosticInfo
{
	const char* Name;
	const char* Description;
	ERuntimeDiagnosticSeverity Severity;
};

UE_API const SRuntimeDiagnosticInfo& GetRuntimeDiagnosticInfo(const ERuntimeDiagnostic Diagnostic);

UE_API FString AsFormattedString(const ERuntimeDiagnostic& Diagnostic, const FText& MessageText);

UE_API void RaiseVerseRuntimeError(const Verse::ERuntimeDiagnostic Diagnostic, const FText& MessageText);
} // namespace Verse

DECLARE_MULTICAST_DELEGATE_TwoParams(FVerseUnwindRuntimeErrorHandler, const Verse::ERuntimeDiagnostic, const FText&);
DECLARE_MULTICAST_DELEGATE_TwoParams(FVerseRuntimeErrorReportHandler, const Verse::ERuntimeDiagnostic, const FText&);

class FVerseRuntimeErrorDelegates
{
public:
#if !WITH_VERSE_VM
	static UE_API FVerseUnwindRuntimeErrorHandler OnUnwindRuntimeError;
#endif

	static UE_API FVerseRuntimeErrorReportHandler OnVerseRuntimeError;
};

// Mechanism for raising a Verse runtime error.
#define RAISE_VERSE_RUNTIME_ERROR_CODE(Diagnostic)                                                    \
	do                                                                                                \
	{                                                                                                 \
		const ::Verse::SRuntimeDiagnosticInfo& DiagnosticInfo = GetRuntimeDiagnosticInfo(Diagnostic); \
		::Verse::RaiseVerseRuntimeError(Diagnostic, FText::FromString(DiagnosticInfo.Description));   \
	}                                                                                                 \
	while (0)

#define RAISE_VERSE_RUNTIME_ERROR(Diagnostic, Message)              \
	do                                                              \
	{                                                               \
		const FText MessageAsText(FText::FromString(Message));      \
		::Verse::RaiseVerseRuntimeError(Diagnostic, MessageAsText); \
	}                                                               \
	while (0)

#define RAISE_VERSE_RUNTIME_ERROR_FORMAT(Diagnostic, FormatString, ...)                             \
	do                                                                                              \
	{                                                                                               \
		const FText MessageAsText(FText::FromString(FString::Printf(FormatString, ##__VA_ARGS__))); \
		::Verse::RaiseVerseRuntimeError(Diagnostic, MessageAsText);                                 \
	}                                                                                               \
	while (0)

#define RETURN_RAISE_VERSE_RUNTIME_ERROR_UNLESS(Cond, ErrCode, ErrStr, ...)                                \
	do                                                                                                     \
	{                                                                                                      \
		if (!(Cond))                                                                                       \
		{                                                                                                  \
			RAISE_VERSE_RUNTIME_ERROR_FORMAT(::Verse::ERuntimeDiagnostic::ErrCode, ErrStr, ##__VA_ARGS__); \
			return;                                                                                        \
		}                                                                                                  \
	}                                                                                                      \
	while (false)

#undef UE_API

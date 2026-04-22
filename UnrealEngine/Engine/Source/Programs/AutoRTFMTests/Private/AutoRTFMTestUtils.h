// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AutoRTFM.h"
#include "AutoRTFMDefines.h"
#include "Containers/StringConv.h"
#include "HAL/Platform.h"
#include "Misc/FeedbackContext.h"

#include <algorithm>
#include <string>
#include <vector>

namespace AutoRTFMTestUtils
{

// Temporarily changes the AutoRTFM retry mode for the lifetime of the FScopedRetry object.
struct FScopedRetry
{
	FScopedRetry(AutoRTFM::ForTheRuntime::EAutoRTFMRetryTransactionState NewRetry)
		: OldRetry(AutoRTFM::ForTheRuntime::GetRetryTransaction())
	{
		AutoRTFM::ForTheRuntime::SetRetryTransaction(NewRetry);
	}

	~FScopedRetry()
	{
		AutoRTFM::ForTheRuntime::SetRetryTransaction(OldRetry);
	}

	AutoRTFM::ForTheRuntime::EAutoRTFMRetryTransactionState OldRetry;
};

// A helper class that for the lifetime of the object, intercepts and records UE_LOG warnings.
class FCaptureWarningContext : private FFeedbackContext
{
public:
	FCaptureWarningContext() : OldContext(GWarn) { GWarn = this; }
	~FCaptureWarningContext() { GWarn = OldContext; }

	UE_AUTORTFM_ALWAYS_OPEN
	void Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const FName& Category) override
	{
		if (Verbosity == ELogVerbosity::Warning)
		{
			Warnings.push_back(std::string(StringCast<ANSICHAR>(V).Get()));
		}
		else
		{
			OldContext->Serialize(V, Verbosity, Category);
		}
	}

	UE_AUTORTFM_ALWAYS_OPEN
	void Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const FName& Category, double Time) override
	{
		if (Verbosity == ELogVerbosity::Warning)
		{
			Warnings.push_back(std::string(StringCast<ANSICHAR>(V).Get()));
		}
		else
		{
			OldContext->Serialize(V, Verbosity, Category, Time);
		}
	}

	UE_AUTORTFM_ALWAYS_OPEN
	const std::vector<std::string>& GetWarnings() const
	{
		return Warnings;
	}

	UE_AUTORTFM_ALWAYS_OPEN
	bool HasWarning(std::string_view Expected) const
	{
		return std::any_of(Warnings.begin(), Warnings.end(), [&](std::string_view Warning)
		{
			return Warning == Expected;
		});
	}

	UE_AUTORTFM_ALWAYS_OPEN
	bool HasWarningSubstring(std::string_view Substr) const
	{
		return std::any_of(Warnings.begin(), Warnings.end(), [&](std::string_view Warning)
		{
			return Warning.find(Substr) != std::string::npos;
		});
	}

private:
	FCaptureWarningContext(const FCaptureWarningContext&) = delete;
	FCaptureWarningContext& operator = (const FCaptureWarningContext&) = delete;

	FFeedbackContext* OldContext = nullptr;
	std::vector<std::string> Warnings;
};

// A helper class that temporarily changes the memory validation level for the lifetime of the
// object, restoring the original level on destruction.
class ScopedMemoryValidationLevel
{
public:
	ScopedMemoryValidationLevel(AutoRTFM::EMemoryValidationLevel NewLevel)
		: PrevLevel(AutoRTFM::ForTheRuntime::GetMemoryValidationLevel())
	{
		AutoRTFM::ForTheRuntime::SetMemoryValidationLevel(NewLevel);
	}

	~ScopedMemoryValidationLevel()
	{
		AutoRTFM::ForTheRuntime::SetMemoryValidationLevel(PrevLevel);
	}
private:
	ScopedMemoryValidationLevel(const ScopedMemoryValidationLevel&) = delete;
	ScopedMemoryValidationLevel& operator = (const ScopedMemoryValidationLevel&) = delete;

	const AutoRTFM::EMemoryValidationLevel PrevLevel;
};

#define AUTORTFM_SCOPED_DISABLE_MEMORY_VALIDATION() \
	AutoRTFMTestUtils::ScopedMemoryValidationLevel DisableMemoryValidation \
		{AutoRTFM::EMemoryValidationLevel::Disabled}; \
	static_assert(true) /* require semicolon */

#define AUTORTFM_SCOPED_ENABLE_MEMORY_VALIDATION_AS_WARNING() \
	AutoRTFMTestUtils::ScopedMemoryValidationLevel EnableMemoryValidationAsWarning \
		{AutoRTFM::EMemoryValidationLevel::Warn}; \
	static_assert(true) /* require semicolon */

static constexpr const std::string_view kMemoryModifiedInOpenWarning =
	"Memory modified in a transaction was also modified in an call to AutoRTFM::Open().\n"
	"This may lead to memory corruption if the transaction is aborted.";

static constexpr const std::string_view kMemoryModifiedInAbortHandlerWarning =
	"Memory modified in a transaction was also modified in the on-abort handler:";

static constexpr const std::string_view kMemoryModifiedInAbortHandlerDtorWarning =
	"Memory modified in a transaction was also modified in the destructor of the on-abort handler:";

static constexpr const std::string_view kMemoryModifiedInCommitHandlerDtorWarning =
	"Memory modified in a transaction was also modified in the destructor of the on-commit handler:";
	
class FScopedEnsureOnInternalAbort final
{
public:
	FScopedEnsureOnInternalAbort(const bool bState)
		: bOriginal(AutoRTFM::ForTheRuntime::GetEnsureOnInternalAbort())
	{
		AutoRTFM::ForTheRuntime::SetEnsureOnInternalAbort(bState);
	}

	~FScopedEnsureOnInternalAbort()
	{
		AutoRTFM::ForTheRuntime::SetEnsureOnInternalAbort(bOriginal);
	}

private:
	FScopedEnsureOnInternalAbort(const FScopedEnsureOnInternalAbort&) = delete;
	FScopedEnsureOnInternalAbort& operator = (const FScopedEnsureOnInternalAbort&) = delete;

	const bool bOriginal;
};

class FScopedInternalAbortAction final
{
public:
	FScopedInternalAbortAction(AutoRTFM::ForTheRuntime::EAutoRTFMInternalAbortActionState State)
		: Original(AutoRTFM::ForTheRuntime::GetInternalAbortAction())
	{
		AutoRTFM::ForTheRuntime::SetInternalAbortAction(State);
	}

	~FScopedInternalAbortAction()
	{
		AutoRTFM::ForTheRuntime::SetInternalAbortAction(Original);
	}

private:
	FScopedInternalAbortAction(const FScopedInternalAbortAction&) = delete;
	FScopedInternalAbortAction& operator = (const FScopedInternalAbortAction&) = delete;

	const AutoRTFM::ForTheRuntime::EAutoRTFMInternalAbortActionState Original;
};

}

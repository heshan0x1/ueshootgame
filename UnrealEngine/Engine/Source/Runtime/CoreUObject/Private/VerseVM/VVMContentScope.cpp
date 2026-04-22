// Copyright Epic Games, Inc. All Rights Reserved.
#include "VerseVM/VVMContentScope.h"
#include "Logging/LogMacros.h"
#include "Misc/StringBuilder.h"
#include "UObject/Package.h"
#include "UObject/ScriptTimeLimiter.h"
#include "VerseVM/VVMInstantiationContext.h"
#include "VerseVM/VVMRuntimeError.h"
#include "VerseVM/VVMVerseHangDetection.h"

DEFINE_LOG_CATEGORY(LogContentScope);

namespace verse
{
void FContentScope::SetCreatorDebugName(const FStringView& InCreatorDebugName)
{
	TStringBuilder<64> Builder;
	Builder.Append(InCreatorDebugName);
	Builder.Append(TEXT("_VerseContentScope"));
	CreatorDebugName = FString(Builder);
}

void FContentScope::SetCreatorDebugNameFromObject(const UObject* Object)
{
	if (Object)
	{
		SetCreatorDebugName(Object->GetName());
	}
	else
	{
		CreatorDebugName = TEXT("FContentScope");
	}
}

FString FContentScope::GetCreatorDebugName() const
{
	return CreatorDebugName;
}

FContentScopeGuard::FContentScopeGuard(const TSharedRef<FContentScope>& Scope)
	: ContentScope(Scope)
{
	ensure(Scope->ShouldExecuteCodeWithThisScope());

	FContentScopeGuard*& ActiveScopeGuard = GetActiveScopeGuard();
	Parent = ActiveScopeGuard;
	ActiveScopeGuard = this;

	UE::FScriptTimeLimiter::Get().StartTimer();

	UE_LOG(LogContentScope, Verbose, TEXT("ContentScope: EnterGuard [%p] with parent [%p] for Scope [%p]: [%s]"), this, Parent, &Scope.Get(), *Scope->GetCreatorDebugName());
}

FContentScopeGuard::~FContentScopeGuard()
{
	FContentScopeGuard*& ActiveScopeGuard = GetActiveScopeGuard();
	if (!ensure(this == ActiveScopeGuard))
	{
		return;
	}

	UE_LOG(LogContentScope, Verbose, TEXT("ContentScope: ExitGuard [%p] with parent [%p] for Scope [%p]: [%s]"), this, Parent, &ContentScope.Get(), *ContentScope->GetCreatorDebugName());

	EnforceExecutionTime();

	ActiveScopeGuard = Parent;
	if (Parent == nullptr)
	{
		for (TFunction<void()>& PendingOnInactiveCallback : PendingOnInactiveCallbacks)
		{
			PendingOnInactiveCallback();
		}
		PendingOnInactiveCallbacks.Empty();
	}

	UE::FScriptTimeLimiter::Get().StopTimer();

	ensure(PendingOnInactiveCallbacks.Num() == 0);
}

void FContentScopeGuard::ScheduleTerminateContentScope(TSharedRef<FContentScope> ContentScope)
{
	OnInactive([ContentScope] {
		ContentScope->Terminate();
	});
}

void FContentScopeGuard::OnInactive(TFunction<void()>&& Callback)
{
	if (FContentScopeGuard* RootGuard = GetRootScopeGuard())
	{
		RootGuard->PendingOnInactiveCallbacks.Add(MoveTemp(Callback));
	}
	else
	{
		Callback();
	}
}

bool FContentScopeGuard::EnforceExecutionTimeForActiveContentScope()
{
#if WITH_VERSE_BPVM
	if (FContentScopeGuard* ActiveScopeGuard = GetActiveScopeGuard())
	{
		return ActiveScopeGuard->EnforceExecutionTime();
	}
#endif
	return true;
}

FContentScopeGuard*& FContentScopeGuard::GetActiveScopeGuard()
{
	thread_local FContentScopeGuard* ActiveScopeGuard = nullptr;
	return ActiveScopeGuard;
}

FContentScopeGuard* FContentScopeGuard::GetRootScopeGuard()
{
	FContentScopeGuard* Guard = nullptr;
	for (FContentScopeGuard* Current = GetActiveScopeGuard(); Current; Current = Current->Parent)
	{
		Guard = Current;
	}
	return Guard;
}

bool FContentScopeGuard::EnforceExecutionTime()
{
	// If we already have terminated the current content scope, don't bother checking further or raising new errors.
	if (!ContentScope->WasTerminated() && UE::FScriptTimeLimiter::Get().HasExceededTimeLimit())
	{
		RAISE_VERSE_RUNTIME_ERROR_FORMAT(
			Verse::ERuntimeDiagnostic::ErrRuntime_ComputationLimitExceeded,
			TEXT("Script (%s) has exceeded its maximum running time. Possible infinite loop or excessive computation."),
			*ContentScope->GetCreatorDebugName());
		return false;
	}
	return true;
}

UObject* GetInstantiationOuter()
{
	UObject* Outer = Verse::FInstantiationScope::Context.Outer;
	if (!Outer && FContentScopeGuard::IsActive())
	{
		Outer = const_cast<UObject*>(FContentScopeGuard::GetActiveScope()->GetInstantiationOuter());
	}
	if (!Outer)
	{
		Outer = GetTransientPackage();
	}
	return Outer;
}

EObjectFlags GetInstantiationFlags()
{
	return Verse::FInstantiationScope::Context.Flags;
}
} // namespace verse
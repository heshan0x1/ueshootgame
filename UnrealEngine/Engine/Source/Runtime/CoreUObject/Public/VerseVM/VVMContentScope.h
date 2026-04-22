// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Array.h"
#include "CoreTypes.h"
#include "Delegates/Delegate.h"
#include "Logging/LogMacros.h"
#include "Misc/AssertionMacros.h"
#include "UObject/GCObject.h"
#include "UObject/WeakObjectPtr.h"
#include "VerseVM/VVMInstantiationContext.h"

#define UE_API COREUOBJECT_API

class UObject;
enum EObjectFlags;
class FReferenceCollector;

DECLARE_MULTICAST_DELEGATE_OneParam(FContentScopeCleanupSignature, bool);

UE_API DECLARE_LOG_CATEGORY_EXTERN(LogContentScope, Warning, All);

namespace Verse
{
struct VTaskGroup;
}

namespace verse
{

class task_group;

class FContentScope
{
public:
	FContentScope(const UObject* InInstantiationOuter)
		: InstantiationOuter(InInstantiationOuter)
	{
	}

	virtual ~FContentScope() {}

	bool ShouldExecuteCodeWithThisScope() const { return !WasTerminated(); }
	bool WasTerminated() const { return bTerminated; }

	const UObject* GetInstantiationOuter() const { return InstantiationOuter.Get(); }
	const UObject* GetWorldContext() const { return InstantiationOuter.Get(); }

#if WITH_VERSE_BPVM
	virtual verse::task_group* GetTaskGroup() = 0;
#else
	virtual Verse::VTaskGroup* GetTaskGroup() = 0;
#endif

	virtual bool HasActiveTasks() const = 0;
	virtual void Terminate() = 0;
	virtual void ResetTerminationState() = 0;

	virtual bool ReserveActorFromQuota() = 0;
	virtual bool CanReserveActorFromQuota() const = 0;
	virtual void ReleaseActorToQuota() = 0;

	virtual bool HasContent() const = 0;

	UE_API FString GetCreatorDebugName() const;
	UE_API void SetCreatorDebugName(const FStringView& InCreatorDebugName);
	UE_API void SetCreatorDebugNameFromObject(const UObject* Object);

	// FORT-687867: Delete this when tag query code is fixed. It depends on the content scope actor and they should not
	// See TagQueryUtils::GetActorFromActiveVerseExecutionContentScope()
	void SetOwner(const UObject* InOwner) { Owner = InOwner; }
	const UObject* GetOwner() const { return Owner.Get(); }

	// Called whenever the content scope is cleaned up/terminated (either normally or abnormally)
	// If it was terminated abnormally, 'true' is passed to the callback
	// All registered callbacks are removed after the event is broadcast
	FContentScopeCleanupSignature OnContentScopeCleanup;

protected:
	// FORT-687867: Delete this when tag query code is fixed. It depends on the content scope actor and they should not
	// See TagQueryUtils::GetActorFromActiveVerseExecutionContentScope()
	TWeakObjectPtr<const UObject> Owner;

	FString CreatorDebugName;

	TWeakObjectPtr<const UObject> InstantiationOuter;
	bool bTerminated = false;
};

class FContentScopeGuard
{
public:
	UE_API explicit FContentScopeGuard(const TSharedRef<FContentScope>& InContent);
	UE_API ~FContentScopeGuard();

	FContentScopeGuard(const FContentScopeGuard&) = delete;
	FContentScopeGuard& operator=(const FContentScopeGuard&) = delete;

	static bool IsActive() { return GetActiveScopeGuard() != nullptr; }
	static const TSharedRef<FContentScope>& GetActiveScope() { return GetActiveScopeGuard()->ContentScope; }

	static UE_API void ScheduleTerminateContentScope(TSharedRef<FContentScope> ContentScope);

	// If there is an active content scope, enqueues Callback to be called when the last active content scope is popped from the stack.
	// IF there is no active content scope, calls Callback immediately.
	static UE_API void OnInactive(TFunction<void()>&& Callback);

	static UE_API bool EnforceExecutionTimeForActiveContentScope();

private:
	static UE_API FContentScopeGuard*& GetActiveScopeGuard();
	static UE_API FContentScopeGuard* GetRootScopeGuard();

	UE_API bool EnforceExecutionTime();

	TSharedRef<FContentScope> ContentScope;
	FContentScopeGuard* Parent = nullptr;

	// Scheduled for shutdown when the outermost scope guard ends.
	TArray<TFunction<void()>> PendingOnInactiveCallbacks;
};

UE_API UObject* GetInstantiationOuter();
UE_API EObjectFlags GetInstantiationFlags();

} // namespace verse

#undef UE_API
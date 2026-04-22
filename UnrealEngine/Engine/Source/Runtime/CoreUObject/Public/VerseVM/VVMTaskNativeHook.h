// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_VERSE_VM || defined(__INTELLISENSE__)

#include "VerseVM/VVMCell.h"
#include "VerseVM/VVMGlobalTrivialEmergentTypePtr.h"
#include "VerseVM/VVMWriteBarrier.h"

namespace Verse
{

struct VTask;
struct FAccessContext;
struct VEmergentType;

struct VTaskNativeHook : VCell
{
	DECLARE_DERIVED_VCPPCLASSINFO(COREUOBJECT_API, VCell);
	COREUOBJECT_API static TGlobalTrivialEmergentTypePtr<&StaticCppClassInfo> GlobalTrivialEmergentType;

	// If desired, this can point to the hook added after this one
	TWriteBarrier<VTaskNativeHook> Next;

	// The lambda can be mutable, therefore invocation is not const
	void Invoke(FAllocationContext Context, VTask* Task)
	{
		Invoker(Context, this, Task);
	}
	void InvokeTransactionally(FAllocationContext Context, VTask* Task)
	{
		TransactionalInvoker(Context, this, Task);
	}

	// WARNING 1: The destructor of the lambda passed in must be thread safe since it can be invoked on the GC thread
	// WARNING 2: Any VCells or UObjects captured by the lambda will not be visited during GC
	template <
		typename FunctorType,
		typename FunctorTypeDecayed = std::decay_t<FunctorType>
			UE_REQUIRES(
				!TIsTFunction<FunctorTypeDecayed>::Value && std::is_invocable_r_v<void, FunctorTypeDecayed, FAllocationContext, VTask*>)>
	static VTaskNativeHook& New(FAllocationContext Context, FunctorType&& Func);

	template <
		typename FunctorType,
		typename FunctorTypeDecayed = std::decay_t<FunctorType>
			UE_REQUIRES(
				!TIsTFunction<FunctorTypeDecayed>::Value && std::is_invocable_r_v<void, FunctorTypeDecayed, FAllocationContext, VTask*>)>
	static VTaskNativeHook& NewOpen(FAllocationContext Context, FunctorType&& Func);

protected:
	using InvokerType = void (*)(FAllocationContext, VTaskNativeHook*, VTask*);
	using DestructorType = void (*)(VTaskNativeHook*);

	VTaskNativeHook(FAllocationContext Context, InvokerType InInvoker, InvokerType InTransactionalInvoker, DestructorType InDestructor)
		: VCell(Context, &GlobalTrivialEmergentType.Get(Context))
		, Invoker(InInvoker)
		, TransactionalInvoker(InTransactionalInvoker)
		, Destructor(InDestructor)
	{
	}

	~VTaskNativeHook()
	{
		Destructor(this);
	}

	template <typename HookType>
	static VTaskNativeHook& NewInternal(FAllocationContext Context, typename HookType::FunctorTypeDecayed&& Func);

private:
	InvokerType Invoker;
	InvokerType TransactionalInvoker;
	DestructorType Destructor;
};

/** Invokes the task from the closed. */
template <typename TFunctorTypeDecayed>
struct AUTORTFM_INFER TTaskNativeHookClosed : VTaskNativeHook
{
	using FunctorTypeDecayed = TFunctorTypeDecayed;

	TTaskNativeHookClosed(FAllocationContext Context, FunctorTypeDecayed&& InFunc)
		: VTaskNativeHook(
			Context,
			/*InInvoker*/ [](FAllocationContext Context, VTaskNativeHook* This, VTask* Task) { static_cast<TTaskNativeHookClosed*>(This)->Func(Context, Task); },
			/*InTransactionalInvoker*/ [](FAllocationContext Context, VTaskNativeHook* This, VTask* Task) {
				AutoRTFM::EContextStatus Status = AutoRTFM::Close([&] {
					static_cast<TTaskNativeHookClosed*>(This)->Func(Context, Task);
				});
				V_DIE_UNLESS(Status == AutoRTFM::EContextStatus::OnTrack); },
			/*InDestructor*/ [](VTaskNativeHook* This) {
				// Destroy only what's not already contained in the base class
				static_cast<TTaskNativeHookClosed*>(This)->Func.~FunctorTypeDecayed(); })
		, Func(Forward<FunctorTypeDecayed>(InFunc))
	{
	}

	FunctorTypeDecayed Func;
};

/** Invokes the task directly from the open. */
template <typename TFunctorTypeDecayed>
struct AUTORTFM_INFER TTaskNativeHookOpen : VTaskNativeHook
{
	using FunctorTypeDecayed = TFunctorTypeDecayed;

	TTaskNativeHookOpen(FAllocationContext Context, FunctorTypeDecayed&& InFunc)
		: VTaskNativeHook(
			Context,
			/*InInvoker*/ [](FAllocationContext Context, VTaskNativeHook* This, VTask* Task) { static_cast<TTaskNativeHookOpen*>(This)->Func(Context, Task); },
			/*InTransactionalInvoker*/ [](FAllocationContext Context, VTaskNativeHook* This, VTask* Task) { static_cast<TTaskNativeHookOpen*>(This)->Func(Context, Task); },
			/*InDestructor*/ [](VTaskNativeHook* This) {
				// Destroy only what's not already contained in the base class
				static_cast<TTaskNativeHookOpen*>(This)->Func.~FunctorTypeDecayed(); })
		, Func(Forward<FunctorTypeDecayed>(InFunc))
	{
	}

	FunctorTypeDecayed Func;
};

template <typename HookType>
VTaskNativeHook& VTaskNativeHook::NewInternal(FAllocationContext Context, typename HookType::FunctorTypeDecayed&& Func)
{
	std::byte* Allocation = std::is_trivially_destructible_v<typename HookType::FunctorTypeDecayed>
							  ? Context.AllocateFastCell(sizeof(HookType))
							  : Context.Allocate(FHeap::DestructorSpace, sizeof(HookType));

	return *new (Allocation) HookType(Context, Forward<typename HookType::FunctorTypeDecayed>(Func));
}

template <
	typename FunctorType,
	typename FunctorTypeDecayed
		UE_REQUIRES(
			!TIsTFunction<FunctorTypeDecayed>::Value && std::is_invocable_r_v<void, FunctorTypeDecayed, FAllocationContext, VTask*>)>
VTaskNativeHook& VTaskNativeHook::New(FAllocationContext Context, FunctorType&& Func)
{
	return NewInternal<TTaskNativeHookClosed<FunctorTypeDecayed>>(Context, Forward<FunctorTypeDecayed>(Func));
}

template <
	typename FunctorType,
	typename FunctorTypeDecayed
		UE_REQUIRES(
			!TIsTFunction<FunctorTypeDecayed>::Value && std::is_invocable_r_v<void, FunctorTypeDecayed, FAllocationContext, VTask*>)>
VTaskNativeHook& VTaskNativeHook::NewOpen(FAllocationContext Context, FunctorType&& Func)
{
	return NewInternal<TTaskNativeHookOpen<FunctorTypeDecayed>>(Context, Forward<FunctorTypeDecayed>(Func));
}

} // namespace Verse

#endif
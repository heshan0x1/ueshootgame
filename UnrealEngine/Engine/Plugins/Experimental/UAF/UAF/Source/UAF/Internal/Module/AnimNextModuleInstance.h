// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AnimNextModule.h"
#include "Module/ModuleHandle.h"
#include "Variables/UAFInstanceVariableDataProxy.h"
#include "UAFAssetInstance.h"
#include "Module/ModuleTickFunction.h"
#include "TraitCore/TraitEvent.h"
#include "TraitCore/TraitEventList.h"
#include "Module/ModuleHandle.h"
#include "Module/UAFModuleInstanceComponent.h"
#include "StructUtils/InstancedStruct.h"
#include "Module/AnimNextModuleInitMethod.h"
#include "Module/TaskRunLocation.h"
#include "AnimNextModuleInstance.generated.h"

struct FAnimNextModuleInstance;
struct FRigUnit_AnimNextRunAnimationGraph_v1;
struct FRigUnit_AnimNextRunAnimationGraph_v2;
class UAnimNextWorldSubsystem;
struct FRigUnit_AnimNextInitializeEvent;
struct FRigUnit_CopyModuleProxyVariables;
struct FAnimNextNotifyDispatcherComponent;
struct FAnimNextGraphInstance;

namespace UE::UAF
{
	struct FModuleEventTickFunction;
	struct FModuleEndTickFunction;
}

namespace UE::UAF::Debug
{
	struct FDebugDraw;
}

namespace UE::UAF
{
	using ModuleInstanceComponentMapType = TMap<FName, TInstancedStruct<FUAFModuleInstanceComponent>>;
}

namespace UE::UAF::Tests
{
	class FVariables_Shared;
}

// Root memory owner of a parameterized schedule 
USTRUCT()
struct FAnimNextModuleInstance : public FUAFAssetInstance
{
	GENERATED_BODY()

	FAnimNextModuleInstance();
	UAF_API FAnimNextModuleInstance(
		UAnimNextModule* InModule,
		UObject* InObject,
		UE::UAF::TPool<FAnimNextModuleInstance>* InPool,
		EAnimNextModuleInitMethod InInitMethod);
	UAF_API ~FAnimNextModuleInstance();

	// Checks to see if this entry is ticking
	bool IsEnabled() const;

	// Enables/disables the ticking of this entry
	void Enable(bool bInEnabled);

	// Queues an input trait event
	// Input events will be processed in the next graph update after they are queued
	UAF_API void QueueInputTraitEvent(FAnimNextTraitEventPtr Event);

	// Queues an output trait event
	// Output events will be processed at the end of the schedule tick
	UAF_API void QueueOutputTraitEvent(FAnimNextTraitEventPtr Event);

	// Get the object that this module is bound to
	UObject* GetObject() const { return Object; }

	// Get the module that this instance represents
	UAF_API const UAnimNextModule* GetModule() const;

#if UE_ENABLE_DEBUG_DRAWING
	// Get the debug draw interface
	UAF_API FRigVMDrawInterface* GetDebugDrawInterface();

	// Set whether to show the module's debug drawing instructions in the viewport
	UAF_API void ShowDebugDrawing(bool bInShowDebugDrawing);
#endif

	// Run a simple task on the GT via FFunctionGraphTask::CreateAndDispatchWhenReady
	UAF_API static void RunTaskOnGameThread(TUniqueFunction<void(void)>&& InFunction);

	// Find the tick function for the specified event
	UE::UAF::FModuleEventTickFunction* FindTickFunctionByName(FName InEventName);
	const UE::UAF::FModuleEventTickFunction* FindTickFunctionByName(FName InEventName) const;

	// Find the first 'user' tick function
	UAF_API UE::UAF::FModuleEventTickFunction* FindFirstUserTickFunction();

	// Run the specified RigVM event
	UAF_API void RunRigVMEvent(FName InEventName, float InDeltaTime);

	// Get the world type that this module was instantiated within
	EWorldType::Type GetWorldType() const { return WorldType; }

	// Get tick functions for this module instance
	UAF_API TArrayView<UE::UAF::FModuleEventTickFunction> GetTickFunctions();

	UE::UAF::FModuleHandle GetHandle() const { return Handle; }

	// Queue a task to run at a particular module event
	// @param	InModuleEvent		The event in the module to run the supplied task relative to. If this is NAME_None or not found then the first 'user' event will be used.
	// @param	InTaskFunction		The function to run
	// @param	InLocation			Where to run the task, before or after
	UAF_API void QueueTask(FName InEventName, TUniqueFunction<void(const UE::UAF::FModuleTaskContext&)>&& InTaskFunction, UE::UAF::ETaskRunLocation InLocation = UE::UAF::ETaskRunLocation::Before);

	// Queue a task to run at a particular module event on some other module
	// @param	InModuleHandle		The handle for the module to queue the task on
	// @param	InModuleEventName	The event in the module to run the supplied task relative to. If this is NAME_None or not found then the first user 'event' will be used.
	// @param	InTaskFunction		The function to run
	// @param	InLocation			Where to run the task, before or after
	UAF_API void QueueTaskOnOtherModule(const UE::UAF::FModuleHandle InOtherModuleHandle, FName InEventName, TUniqueFunction<void(const UE::UAF::FModuleTaskContext&)>&& InTaskFunction, UE::UAF::ETaskRunLocation InLocation = UE::UAF::ETaskRunLocation::Before);

private:
	// Setup the entry
	UAF_API void Initialize();

	// Clear data that binds the schedule to a runtime (e.g. tick functions) and any instance data
	void ResetBindingsAndInstanceData();

	// Remove all of our tick function's dependencies
	void RemoveAllTickDependencies();

	// Flip proxy variable buffers then copy any dirty values
	void CopyProxyVariables();

#if ANIMNEXT_TRACE_ENABLED
	// trace this module if it hasn't already been traced this frame
	UAF_API void Trace();
#endif

#if WITH_EDITOR
	// Resets internal state if the module we are bound to is recompiled in editor
	void OnCompileJobFinished();
#endif

	// Give the module components a chance to handle events and call their OnEndExecution extension point
	void EndExecution(float InDeltaTime);

	// Handles trait events at the module level.
	// Called in the module end tick function before any events are dispatched.
	void RaiseTraitEvents(const UE::UAF::FTraitEventList& EventList);

	// Set the value of the specified variable via the proxy double-buffer. Called from any thread.
	EPropertyBagResult SetProxyVariable(const FAnimNextVariableReference& InVariable, const FAnimNextParamType& InType, TConstArrayView<uint8> InData);

	// Accesses value of the specified variable via the proxy double-buffer. Called from any thread.
	EPropertyBagResult WriteProxyVariable(const FAnimNextVariableReference& InVariable, const FAnimNextParamType& InType, TFunctionRef<void(TArrayView<uint8>)> InFunction);

private:
	friend UE::UAF::FModuleEventTickFunction;
	friend UE::UAF::FModuleEndTickFunction;
	friend FRigUnit_AnimNextRunAnimationGraph_v1;
	friend FRigUnit_AnimNextRunAnimationGraph_v2;
	friend UAnimNextWorldSubsystem;
	friend FRigUnit_AnimNextInitializeEvent;
	friend FRigUnit_CopyModuleProxyVariables;
	friend FAnimNextNotifyDispatcherComponent;
	friend FAnimNextGraphInstance;
	friend UE::UAF::Tests::FVariables_Shared;

	// Object this entry is bound to
	UPROPERTY(Transient)
	TObjectPtr<UObject> Object = nullptr;

	// The pool that this module instance exists in
	UE::UAF::TPool<FAnimNextModuleInstance>* Pool = nullptr;

	// Copy of the handle that represents this entry to client systems
	UE::UAF::FModuleHandle Handle;

	// Pre-allocated graph of tick functions
	TArray<UE::UAF::FModuleEventTickFunction> TickFunctions;

	// Input event list to be processed on the next update
	UE::UAF::FTraitEventList InputEventList;

	// Output event list to be processed at the end of the schedule tick
	UE::UAF::FTraitEventList OutputEventList;

	// Lock to ensure event list actions are thread safe
	FRWLock EventListLock;

	// Lock to ensure proxy access is thread safe
	FRWLock ProxyLock;

	// Proxy public variables, double-buffered
	UPROPERTY(Transient)
	FUAFInstanceVariableDataProxy ProxyVariables[2];

	// Index into current buffer to write the proxy variables
	int32 ProxyWriteIndex = 0;

#if UE_ENABLE_DEBUG_DRAWING
	// Debug draw support
	// TODO: Move this to a 'module component' once that code is checked in
	TUniquePtr<UE::UAF::Debug::FDebugDraw> DebugDraw;
#endif

	enum class EInitState : uint8
	{
		NotInitialized,

		CreatingTasks,

		BindingTasks,

		SetupVariables,

		PendingInitializeEvent,

		FirstUpdate,

		Initialized
	};

	// Current initialization state
	EInitState InitState = EInitState::NotInitialized;

	enum class ERunState : uint8
	{
		NotInitialized,

		Running,

		Paused,
	};

	// Current running state
	ERunState RunState = ERunState::NotInitialized;

	// Transition to the specified init state, verifying that the current state is valid
	void TransitionToInitState(EInitState InNewState);

	// Transition to the specified run state, verifying that the current state is valid
	void TransitionToRunState(ERunState InNewState);

	// How this entry initializes
	EAnimNextModuleInitMethod InitMethod = EAnimNextModuleInitMethod::InitializeAndPauseInEditor;

	// Whether this represents an editor object 
	EWorldType::Type WorldType = EWorldType::None;

#if WITH_EDITOR
	// Whether we are currently recreating this instance because of compilation/reinstancing
	bool bIsRecreatingOnCompile : 1 = false;
#endif

private:
#if ANIMNEXT_TRACE_ENABLED
	bool bTracedThisFrame = false;
#endif
};

template<>
struct TStructOpsTypeTraits<FAnimNextModuleInstance> : public TStructOpsTypeTraitsBase2<FAnimNextModuleInstance>
{
	enum
	{
		WithCopy = false
	};
};

#if ANIMNEXT_TRACE_ENABLED
#define TRACE_ANIMNEXT_MODULE(ModuleInstance) ModuleInstance.Trace();
#else
#define TRACE_ANIMNEXT_MODULE(ModuleInstance)
#endif

// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AnimNextPool.h"
#include "EngineDefines.h"
#include "Misc/TVariant.h"
#include "Module/ModuleHandle.h"
#include "Module/ModuleInstanceUtils.h"
#include "Module/TaskRunLocation.h"
#include "Subsystems/WorldSubsystem.h"
#include "AnimNextWorldSubsystem.generated.h"

#define UE_API UAF_API

class UAnimNextModule;
class UAnimNextRigVMAsset;
enum class EAnimNextModuleInitMethod : uint8;
enum class EPropertyBagResult : uint8;
struct FAnimNextParamType;
struct FAnimNextTraitEvent;
struct FAnimNextVariableReference;

namespace UE::UAF
{
struct FInjectionRequest;
struct FModuleContext;
struct FModuleEventTickFunction;
struct FModuleTaskContext;

// A queued action to complete next frame
struct FModulePendingAction
{
	enum class EType : int32
	{
		None = 0,
		ReleaseHandle,			// Payload = FModulePayloadNone
		EnableHandle,			// Payload = bool
		EnableDebugDrawing,		// Payload = bool
	};

	// Marker struct for an 'empty' payload
	struct FModulePayloadNone
	{
	};

	FModulePendingAction() = default;

	FModulePendingAction(FModuleHandle InHandle, EType InType)
		: Handle(InHandle)
		, Type(InType)
	{
		Payload.Set<FModulePayloadNone>(FModulePayloadNone());
	}

	template<typename PayloadType>
	FModulePendingAction(FModuleHandle InHandle, EType InType, PayloadType InPayload)
		: Handle(InHandle)
		, Type(InType)
	{
		Payload.Set<PayloadType>(InPayload);
	}

	TVariant<FModulePayloadNone, bool, UE::UAF::FModuleHandle> Payload;

	FModuleHandle Handle;

	EType Type = EType::None;
};

}

// Represents AnimNext systems to the gameplay framework
UCLASS(MinimalAPI, Abstract)
class UAnimNextWorldSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	UE_API UAnimNextWorldSubsystem();

	UE_API virtual void BeginDestroy() override;

	// Get the subsystem for the specified object's world
	static UE_API UAnimNextWorldSubsystem* Get(UObject* InObject);

	static UE_API void AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector);

	// Dependency type passed to AddDependencyHandle etc.
	enum class EDependency : uint8
	{
		// Dependency runs before the specified event
		Prerequisite,
		// Dependency runs after the specified event
		Subsequent
	};

private:
	// UWorldSubsystem interface
	UE_API virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

	UE_API bool IsValidHandle(UE::UAF::FModuleHandle InHandle) const;

	UE_API void FlushPendingActions();

protected:
#if WITH_EDITOR
	// Refresh any entries that use the provided asset as it has been recompiled.
	UE_API void OnCompileJobFinished(UAnimNextRigVMAsset* InAsset);
#endif

	// Register a handle to the subsystem
	UE_API void RegisterHandle(UE::UAF::FModuleHandle& InOutHandle, UAnimNextModule* InModule, UObject* InObject, EAnimNextModuleInitMethod InitMethod);

	// Unregister a handle from the subsystem
	UE_API void UnregisterHandle(UE::UAF::FModuleHandle& InOutHandle);

	// Returns whether the module represented by the supplied handle is enabled.
	// If there are pending actions they will take precedent over the actual current state.
	UE_API bool IsHandleEnabled(UE::UAF::FModuleHandle InHandle) const;

	// Enables or disables the module represented by the supplied handle
	// This operation is deferred until the next time the schedule ticks
	UE_API void EnableHandle(UE::UAF::FModuleHandle InHandle, bool bInEnabled);

#if UE_ENABLE_DEBUG_DRAWING
	// Enables or disables the module's debug drawing
	// This operation is deferred until the next time the schedule ticks
	UE_API void ShowDebugDrawingHandle(UE::UAF::FModuleHandle InHandle, bool bInShowDebugDrawing);
#endif

	// Queue a task to run at a particular point in a schedule
	// @param	InHandle			The handle to execute the task on
	// @param	InModuleEventName	The name of the event in the module to run the supplied task relative to. If this is NAME_None, then the first valid event will be used.
	// @param	InTaskFunction		The function to run
	// @param	InLocation			Where to run the task, before or after
	UE_API void QueueTaskHandle(UE::UAF::FModuleHandle InHandle, FName InModuleEventName, TUniqueFunction<void(const UE::UAF::FModuleTaskContext&)>&& InTaskFunction, UE::UAF::ETaskRunLocation InLocation = UE::UAF::ETaskRunLocation::Before);

	// Queue an input trait event on the supplied handle
	UE_API void QueueInputTraitEventHandle(UE::UAF::FModuleHandle InHandle, TSharedPtr<FAnimNextTraitEvent> Event);

	// Find the const tick function for the specified event. Useful to analyze/log prerequisites. Use AddDependencyHandle, RemoveDependencyHandle to actually modify dependencies
	// @param	InHandle			The handle of the module
	// @param	InEventName			The event associated to the wanted tick function
	UE_API const FTickFunction* FindTickFunctionHandle(UE::UAF::FModuleHandle InHandle, FName InEventName) const;

	// Add a dependency on a tick function to the specified event
	// @param	InHandle			The handle of the module whose dependencies are being modified
	// @param	InObject			The object that owns the tick function
	// @param	InTickFunction		The tick function to depend on
	// @param	InEventName			The event to add the dependency to
	// @param	InDependency		The kind of dependency to add
	UE_API void AddDependencyHandle(UE::UAF::FModuleHandle InHandle, UObject* InObject, FTickFunction& InTickFunction, FName InEventName, EDependency InDependency);

	// Remove a dependency on a tick function from the specified event
	// @param	InHandle			The handle of the module whose dependencies are being modified
	// @param	InObject			The object that owns the tick function
	// @param	InTickFunction		The tick function that was depended on
	// @param	InEventName			The event to remove the dependency from
	// @param	InDependency		The kind of dependency to remove
	UE_API void RemoveDependencyHandle(UE::UAF::FModuleHandle InHandle, UObject* InObject, FTickFunction& InTickFunction, FName InEventName, EDependency InDependency);

	// Add a dependency on a module event to the specified module event
	// @param	InHandle			The handle of the module whose dependencies are being modified
	// @param	InEventName			The event to add the dependency to/from
	// @param	OtherHandle			The handle of the other module whose dependencies are being modified
	// @param	OtherEventName		The other module's event to add the dependency to/from
	// @param	InDependency		The kind of dependency to add
	UE_API void AddModuleEventDependencyHandle(UE::UAF::FModuleHandle InHandle, FName InEventName, UE::UAF::FModuleHandle OtherHandle, FName OtherEventName, EDependency InDependency);

	// Remove a dependency on a module event from the specified module event
	// @param	InHandle			The handle of the module whose dependencies are being modified
	// @param	InEventName			The event to remove the dependency to/from
	// @param	OtherHandle			The handle of the other module whose dependencies are being modified
	// @param	OtherEventName		The other module's event to remove the dependency to/from
	// @param	InDependency		The kind of dependency to remove
	UE_API void RemoveModuleEventDependencyHandle(UE::UAF::FModuleHandle InHandle, FName InEventName, UE::UAF::FModuleHandle OtherHandle, FName OtherEventName, EDependency InDependency);

	// Set the value of the specified variable
	// @param	InHandle			The handle of the module whose variable we are setting
	// @param	InVariable			The variable we want to set
	// @param	InType				The type of the variable we want to set
	// @param	InData				The data to set the variable with
	// @return true if the variable was set successfully
	UE_API EPropertyBagResult SetVariableHandle(UE::UAF::FModuleHandle InHandle, const FAnimNextVariableReference& InVariable, const FAnimNextParamType& InType, TConstArrayView<uint8> InData);

	// Accesses the variable of the specified name for writing.
	// General read access is not provided via this API due to the current double-buffering strategy used to communicate variable writes to worker threads.
	// This is intended to allow for copy-free writing of larger data structures & arrays, rather than read access.
	// @param	InHandle			The handle of the module whose variable we are setting
	// @param	InVariable			The variable we want to access
	// @param	InType				The type of the variable we want to access
	// @param	InFunction			Function that will be called to allow modification of the variables
	// @return true if the variable was accessed successfully
	UE_API EPropertyBagResult WriteVariableHandle(UE::UAF::FModuleHandle InHandle, const FAnimNextVariableReference& InVariable, const FAnimNextParamType& InType, TFunctionRef<void(TArrayView<uint8>)> InFunction);

protected:
	friend struct FRigUnit_AnimNextInitializeEvent;
	friend struct UE::UAF::FModuleEventTickFunction;
	friend struct UE::UAF::FInjectionRequest;
	friend EPropertyBagResult UE::UAF::SetModuleVariable(UE::UAF::FModuleContext&&, const FAnimNextVariableReference&, const FAnimNextParamType&, TConstArrayView<uint8>);
	friend EPropertyBagResult UE::UAF::WriteModuleVariable(UE::UAF::FModuleContext&&, const FAnimNextVariableReference&, const FAnimNextParamType&, TFunctionRef<void(TArrayView<uint8>)>);

	// Currently running instances, pooled
	UE::UAF::TPool<FAnimNextModuleInstance> Instances;

	// Queued actions
	TArray<UE::UAF::FModulePendingAction> PendingActions;

	// Locks and validators for concurrent modifications
	FRWLock InstancesLock;
	FRWLock PendingLock;
	UE_MT_DECLARE_RW_FULLY_RECURSIVE_ACCESS_DETECTOR(InstancesAccessDetector);
	UE_MT_DECLARE_RW_ACCESS_DETECTOR(PendingActionsAccessDetector);

#if WITH_EDITOR
	// Handle used to hook asset compilation
	FDelegateHandle OnCompileJobFinishedHandle;
#endif

	// Handle used to hook into pre-world tick
	FDelegateHandle OnWorldPreActorTickHandle;

	// Cached delta time
	float DeltaTime;
};

#undef UE_API
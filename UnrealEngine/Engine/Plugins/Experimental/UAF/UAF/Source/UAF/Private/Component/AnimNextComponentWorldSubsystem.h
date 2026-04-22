// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AnimNextPool.h"
#include "Component/AnimNextWorldSubsystem.h"
#include "Module/ModuleHandle.h"
#include "Module/ModuleTaskContext.h"
#include "AnimNextComponentWorldSubsystem.generated.h"

class UAnimNextComponent;

// Represents AnimNext systems to the AActor/UActorComponent gameplay framework
UCLASS()
class UAnimNextComponentWorldSubsystem : public UAnimNextWorldSubsystem
{
	GENERATED_BODY()

	friend class UAnimNextComponent;

	// Register a component to the subsystem
	void Register(UAnimNextComponent* InComponent);

	// Unregister a component to the subsystem
	// The full release of the module referenced by the component's handle will be deferred after this call is made
	void Unregister(UAnimNextComponent* InComponent);

	// Returns whether the module is enabled.
	bool IsEnabled(const UAnimNextComponent* InComponent) const;

	// Enables or disables the module represented by the supplied handle
	// This operation is deferred until the next time the schedule ticks
	void SetEnabled(UAnimNextComponent* InComponent, bool bInEnabled);

#if UE_ENABLE_DEBUG_DRAWING
	// Enables or disables debug drawing for the module represented by the supplied handle
	// This operation is deferred until the next time the schedule ticks
	void ShowDebugDrawing(UAnimNextComponent* InComponent, bool bInShowDebugDrawing);
#endif

	// Queue a task to run at a particular point in a schedule
	// @param	InComponent			The component to execute the task on
	// @param	InModuleEventName	The name of the event in the module to run the supplied task relative to. If this is NAME_None, then the first valid event will be used.
	// @param	InTaskFunction		The function to run
	// @param	InLocation			Where to run the task, before or after
	void QueueTask(UAnimNextComponent* InComponent, FName InModuleEventName, TUniqueFunction<void(const UE::UAF::FModuleTaskContext&)>&& InTaskFunction, UE::UAF::ETaskRunLocation InLocation = UE::UAF::ETaskRunLocation::Before);

	// Queue an input trait event
	void QueueInputTraitEvent(UAnimNextComponent* InComponent, FAnimNextTraitEventPtr Event);

	// Find the component tick function for the specified event
	// @param	InComponent			The component being searched for the tick function
	// @param	InEventName			The event associated to the wanted tick function
	const FTickFunction* FindTickFunction(const UAnimNextComponent* InComponent, FName InEventName) const;

	// Add a dependency on a tick function to the specified event
	// @param	InComponent			The component whose dependencies are being modified
	// @param	InObject			The object that owns the tick function
	// @param	InTickFunction		The tick function to depend on
	// @param	InEventName			The event to add the dependency to
	void AddDependency(UAnimNextComponent* InComponent, UObject* InObject, FTickFunction& InTickFunction, FName InEventName, EDependency InDependency);

	// Remove a dependency on a tick function from the specified event
	// @param	InComponent			The component whose dependencies are being modified
	// @param	InObject			The object that owns the tick function
	// @param	InTickFunction		The tick function that was depended on
	// @param	InEventName			The event to remove the dependency from
	void RemoveDependency(UAnimNextComponent* InComponent, UObject* InObject, FTickFunction& InTickFunction, FName InEventName, EDependency InDependency);

	// Add a dependency on a module event to the specified module event
	// @param	InComponent			The component whose dependencies are being modified
	// @param	InEventName			The event to add the dependency to/from
	// @param	OtherComponent		The other component whose dependencies are being modified
	// @param	OtherEventName		The other module's event to add the dependency to/from
	// @param	InDependency		The kind of dependency to add
	void AddModuleEventDependency(UAnimNextComponent* InComponent, FName InEventName, UAnimNextComponent* OtherComponent, FName OtherEventName, EDependency InDependency);

	// Remove a dependency on a module event to the specified module event
	// @param	InComponent			The component whose dependencies are being modified
	// @param	InEventName			The event to remove the dependency to/from
	// @param	OtherComponent		The other component whose dependencies are being modified
	// @param	OtherEventName		The other module's event to remove the dependency to/from
	// @param	InDependency		The kind of dependency to remove
	void RemoveModuleEventDependency(UAnimNextComponent* InComponent, FName InEventName, UAnimNextComponent* OtherComponent, FName OtherEventName, EDependency InDependency);

	// Set the value of the specified variable
	// @param	InComponent			The component whose variable we are setting
	// @param	InVariable			The variable we want to set
	// @param	InType				The type of the variable we want to set
	// @param	InData				The data to set the variable with
	// @return true if the variable was set successfully
	EPropertyBagResult SetVariable(UAnimNextComponent* InComponent, const FAnimNextVariableReference& InVariable, const FAnimNextParamType& InType, TConstArrayView<uint8> InData);

	// Accesses the variable of the specified name for writing.
	// General read access is not provided via this API due to the current double-buffering strategy used to communicate variable writes to worker threads.
	// This is intended to allow for copy-free writing of larger data structures & arrays, rather than read access. 
	// @param	InComponent			The component whose variable we are setting
	// @param	InVariable			The variable we want to access
	// @param	InType				The type of the variable we want to access
	// @param	InFunction			Function that will be called to allow modification of the variable
	// @return true if the variable was accessed successfully
	EPropertyBagResult WriteVariable(UAnimNextComponent* InComponent, const FAnimNextVariableReference& InVariable, const FAnimNextParamType& InType, TFunctionRef<void(TArrayView<uint8>)> InFunction);
};
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/ActorComponent.h"
#include "Module/ModuleHandle.h"
#include "Param/ParamType.h"
#include "TraitCore/TraitEvent.h"
#include "Module/AnimNextModuleInitMethod.h"
#include "Module/ModuleTaskContext.h"
#include "Module/TaskRunLocation.h"

#include "AnimNextComponent.generated.h"

namespace UE::Workspace
{
	class IWorkspaceViewportController;
}

struct FAnimNextComponentInstanceData;
class UAnimNextComponentWorldSubsystem;
class UAnimNextModule;
struct AnimNextVariableReference;
// TODO: if a public API is implemented, the forward declaration of this struct would no longer be needed.
struct FAnimMixerUtils;

namespace UE::UAF::UncookedOnly
{
	struct FUtils;
}

namespace UE::UAF::Editor
{
	class FAssetWizard;
	class FSystemViewportController;
	class FAnimGraphViewportController;
}

UCLASS(MinimalAPI, meta = (BlueprintSpawnableComponent))
class UAnimNextComponent : public UActorComponent
{
	GENERATED_BODY()

	// UActorComponent interface
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:
	// Sets a module variable's value.
	// @param    Name     The name of the variable to set
	// @param    Value    The value to set the variable to
	UE_DEPRECATED(5.6, "This function is no longer used, please use SetVariableReference (for Blueprint) or SetVariable that takes a FAnimNextVariableReference")
	UFUNCTION(BlueprintCallable, Category = "UAF", DisplayName = "Set Variable", CustomThunk, meta = (CustomStructureParam = Value, UnsafeDuringActorConstruction, DeprecatedFunction, DeprecationMessage = "This function has been deprecated, please use Set Variable that takes a variable reference instead"))
	UAF_API void BlueprintSetVariable(FName Name, int32 Value);

	// Sets a module variable's value.
	// @param    Name     The variable to set
	// @param    Value    The value to set the variable to
	UFUNCTION(BlueprintCallable, BlueprintInternalUseOnly, Category = "UAF", DisplayName = "Set Variable", CustomThunk, meta = (CustomStructureParam = Value, AutoCreateRefTerm = "Variable, Value", UnsafeDuringActorConstruction))
	UAF_API void BlueprintSetVariableReference(const FAnimNextVariableReference& Variable, const int32& Value);

	// Set a variable.
	// @param    InVariable         The variable to set
	// @param    InValue            The value to set the variable to
	// @return true if the variable was set correctly
	template<typename ValueType>
	bool SetVariable(const FAnimNextVariableReference& InVariable, const ValueType& InValue)
	{
		return SetVariableInternal(InVariable, FAnimNextParamType::GetType<ValueType>(), TConstArrayView<uint8>(reinterpret_cast<const uint8*>(&InValue), sizeof(ValueType))) == EPropertyBagResult::Success;
	}

	// Accesses a variable for writing.
	// General read access is not provided via this API due to the current double-buffering strategy used to communicate variable writes to worker threads.
	// This is intended to allow for copy-free writing of larger data structures & arrays, rather than read access. 
	// @param    InVariable         The variable to set
	// @param    InFunction         The function used to modify the variable, called back immediately on the game thread if the variable exists.
	// @return true if the variable was accessed successfully
	template<typename ValueType>
	bool WriteVariable(const FAnimNextVariableReference& InVariable, TFunctionRef<void(ValueType&)> InFunction)
	{
		return WriteVariableInternal(InVariable, FAnimNextParamType::GetType<ValueType>(), [&InFunction](TArrayView<uint8> InData)
		{
			InFunction(*reinterpret_cast<ValueType*>(InData.GetData()));
		}) == EPropertyBagResult::Success;
	}

	// Whether this component is currently updating
	UFUNCTION(BlueprintCallable, Category = "UAF")
	UAF_API bool IsEnabled() const;

	// Enable or disable this component's update
	UFUNCTION(BlueprintCallable, Category = "UAF")
	UAF_API void SetEnabled(bool bEnabled);

	// Enable or disable debug drawing. Note only works in builds with UE_ENABLE_DEBUG_DRAWING enabled
	UFUNCTION(BlueprintCallable, Category = "UAF")
	UAF_API void ShowDebugDrawing(bool bShowDebugDrawing);

	// Queue a task to run during execution 
	UAF_API void QueueTask(FName InModuleEventName, TUniqueFunction<void(const UE::UAF::FModuleTaskContext&)>&& InTaskFunction, UE::UAF::ETaskRunLocation InLocation = UE::UAF::ETaskRunLocation::Before);

	// Queues an input trait event
	// Input events will be processed in the next graph update after they are queued
	UAF_API void QueueInputTraitEvent(FAnimNextTraitEventPtr Event);

	// Get the handle to the registered module
	UE::UAF::FModuleHandle GetModuleHandle() const { return ModuleHandle; }

	// Find the tick function for the specified event
	// @param	InEventName			The event associated to the wanted tick function
	UAF_API const FTickFunction* FindTickFunction(FName InEventName) const;

	// Add a prerequisite tick function dependency to the specified event
	// @param	InObject			The object that owns the tick function
	// @param	InTickFunction		The tick function to depend on
	// @param	InEventName			The event to add the dependency to
	UAF_API void AddPrerequisite(UObject* InObject, FTickFunction& InTickFunction, FName InEventName);

	// Add a prerequisite dependency on the component's primary tick function to the specified event
	// The component will tick before the event
	// @param	Component			The component to add as a prerequisite 
	// @param	EventName			The event to add the dependency to
	UFUNCTION(BlueprintCallable, Category = "UAF")
	UAF_API void AddComponentPrerequisite(UActorComponent* Component, FName EventName);

	// Add a subsequent tick function dependency to the specified event
	// @param	InObject			The object that owns the tick function
	// @param	InTickFunction		The tick function to depend on
	// @param	InEventName			The event to add the dependency to
	UAF_API void AddSubsequent(UObject* InObject, FTickFunction& InTickFunction, FName InEventName);

	// Add a subsequent dependency on the component's primary tick function to the specified event
	// The component will tick after the event
	// @param	Component			The component to add as a subsequent of the event
	// @param	EventName			The event to add the dependency to
	UFUNCTION(BlueprintCallable, Category = "UAF")
	UAF_API void AddComponentSubsequent(UActorComponent* Component, FName EventName);

	// Remove a prerequisite tick function dependency from the specified event
	// @param	InObject			The object that owns the tick function
	// @param	InTickFunction		The tick function that was depended on
	// @param	InEventName			The event to remove the dependency from
	UAF_API void RemovePrerequisite(UObject* InObject, FTickFunction& InTickFunction, FName InEventName);

	// Remove a prerequisite on the component's primary tick function from the specified event
	// @param	Component			The component to remove as a prerequisite 
	// @param	EventName			The event to add the dependency to
	UFUNCTION(BlueprintCallable, Category = "UAF")
	UAF_API void RemoveComponentPrerequisite(UActorComponent* Component, FName EventName);
	
	// Remove a prerequisite tick function dependency from the specified event
	// @param	InObject			The object that owns the tick function
	// @param	InTickFunction		The tick function that was depended on
	// @param	InEventName			The event to remove the dependency from
	UAF_API void RemoveSubsequent(UObject* InObject, FTickFunction& InTickFunction, FName InEventName);

	// Remove a subsequent dependency on the component's primary tick function from the specified event
	// @param	Component			The component to remove as a subsequent of the event
	// @param	EventName			The event to add the dependency to
	UFUNCTION(BlueprintCallable, Category = "UAF")
	UAF_API void RemoveComponentSubsequent(UActorComponent* Component, FName EventName);
	
	// Add a prerequisite anim next event dependency to the specified event
	// @param	InEventName			The event name in this component
	// @param	InTickFunction		The other component we want a prerequisite on
	// @param	InEventName			The other component's event name
	UFUNCTION(BlueprintCallable, Category = "UAF")
	UAF_API void AddModuleEventPrerequisite(FName InEventName, UAnimNextComponent* OtherAnimNextComponent, FName OtherEventName);

	// Add a subsequent anim next event dependency to the specified event
	// @param	InEventName			The event name in this component
	// @param	InTickFunction		The other component we want to add a prerequisite to
	// @param	InEventName			The other component's event name
	UFUNCTION(BlueprintCallable, Category = "UAF")
	UAF_API void AddModuleEventSubsequent(FName InEventName, UAnimNextComponent* OtherAnimNextComponent, FName OtherEventName);

	// Remove a prerequisite anim next event dependency from the specified event
	// @param	InEventName			The event name in this component
	// @param	InTickFunction		The other component we want to remove a prerequisite from
	// @param	InEventName			The other component's event name
	UFUNCTION(BlueprintCallable, Category = "UAF")
	UAF_API void RemoveModuleEventPrerequisite(FName InEventName, UAnimNextComponent* OtherAnimNextComponent, FName OtherEventName);

	// Remove a subsequent anim next event dependency from the specified event
	// @param	InEventName			The event name in this component
	// @param	InTickFunction		The other component we want to remove a prerequisite to
	// @param	InEventName			The other component's event name
	UFUNCTION(BlueprintCallable, Category = "UAF")
	UAF_API void RemoveModuleEventSubsequent(FName InEventName, UAnimNextComponent* OtherAnimNextComponent, FName OtherEventName);

	UFUNCTION(BlueprintCallable, Category = "UAF", meta=(DisplayName = "Get Module Handle"))
	UAF_API FAnimNextModuleHandle BlueprintGetModuleHandle() const;

	UAF_API TObjectPtr<UAnimNextModule> GetModule() const;

private:
	UAF_API void SetModule(TObjectPtr<UAnimNextModule> InModule);

	UAF_API void RegisterWithSubsystem();
	UAF_API void UnregisterWithSubsystem();

	UAF_API bool IsModuleValid();

	DECLARE_FUNCTION(execBlueprintSetVariable);
	DECLARE_FUNCTION(execBlueprintSetVariableReference);

	// Sets the value of the specified variable
	UAF_API EPropertyBagResult SetVariableInternal(const FAnimNextVariableReference& InVariable, const FAnimNextParamType& InType, TConstArrayView<uint8> InNewValue);

	// Accesses value of the specified variable for writing
	UAF_API EPropertyBagResult WriteVariableInternal(const FAnimNextVariableReference& InVariable, const FAnimNextParamType& InType, TFunctionRef<void(TArrayView<uint8>)> InFunction);

private:
	friend struct UE::UAF::UncookedOnly::FUtils;
	friend struct FAssetBlueprintTests;
	friend class UAnimNextComponentWorldSubsystem;
	friend class UE::UAF::Editor::FAssetWizard;
	// TODO ideally there would be a public API for the things FAnimMixerUtils does, so it wouldn't be necessary.
	friend struct FAnimMixerUtils;
	friend class UE::UAF::Editor::FSystemViewportController;
	friend class UE::UAF::Editor::FAnimGraphViewportController;

	// The UAF system that this component will run
	UPROPERTY(EditAnywhere, Category="System", DisplayName="System")
	TObjectPtr<UAnimNextModule> Module = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UAnimNextComponentWorldSubsystem> Subsystem = nullptr;

	// Handle to the registered module
	UE::UAF::FModuleHandle ModuleHandle;

	// How to initialize the system
	UPROPERTY(EditAnywhere, Category="System")
	EAnimNextModuleInitMethod InitMethod = EAnimNextModuleInitMethod::InitializeAndPauseInEditor;

	/** When checked, the system's debug drawing instructions are drawn in the viewport */
	UPROPERTY(EditAnywhere, Category = "Rendering")
	uint8 bShowDebugDrawing : 1 = false;
};

// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InstanceTaskContext.h"

#include "TraitCore/TraitEvent.h"

struct FUAFModuleInstanceComponent;
struct FAnimNextModuleInstance;
struct FInstancedPropertyBag;
struct FAnimNextModuleInjectionComponent;

namespace UE::UAF
{
	struct FScheduleContext;
	enum class EParameterScopeOrdering : int32;
	struct FModuleEventTickFunction;
}

namespace UE::UAF
{

// Context passed to schedule task callbacks
struct FModuleTaskContext : public FInstanceTaskContext
{
public:
	// Queues an input trait event
	// Input events will be processed in the next graph update after they are queued
	UAF_API void QueueInputTraitEvent(FAnimNextTraitEventPtr Event) const;

	// Access a module instance component of the specified type. If the component exists, then InFunction will be called
	UAF_API void TryAccessComponent(UScriptStruct* InComponentType, TFunctionRef<void(FUAFModuleInstanceComponent&)> InFunction) const;

	template<typename ComponentType>
	void TryAccessComponent(TFunctionRef<void(ComponentType&)> InFunction) const
	{
		TryAccessComponent(TBaseStructure<ComponentType>::Get(), [&InFunction](FUAFModuleInstanceComponent& InComponent)
		{
			InFunction(static_cast<ComponentType&>(InComponent));
		});
	}

	UAF_API FAnimNextModuleInstance* const GetModuleInstance() const;

private:
	FModuleTaskContext(FAnimNextModuleInstance& InModuleInstance);

	// The module instance currently running
	FAnimNextModuleInstance& ModuleInstance;

	friend UE::UAF::FModuleEventTickFunction;
	friend ::FAnimNextModuleInjectionComponent;
};

}